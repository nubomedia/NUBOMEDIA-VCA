
#include "kmsnosedetect.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>
#include <string>
#include <unistd.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <sys/time.h>
#include <commons/kmsserializablemeta.h>
#include <sstream>

#include <commons/kms-core-marshal.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>


#define PLUGIN_NAME "nubonosedetector"
#define FACE_WIDTH 160
#define NOSE_WIDTH 320

#define DEFAULT_FILTER_TYPE (KmsNoseDetectType)0


#define FACE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_alt.xml"
#define NOSE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_nose.xml"

#define TOP_PERCENTAGE 25
#define DOWN_PERCENTAGE 10
#define SIDE_PERCENTAGE 25
#define NUM_FRAMES_TO_PROCESS 10
#define PROCESS_ALL_FRAMES 4
#define DEFAULT_SCALE_FACTOR 25
#define FACE_TYPE "face"
#define NOSE_SCALE_FACTOR 1.1
#define GOP 4
#define DEFAULT_EUCLIDEAN_DIS 6
#define SERVER_EVENTS 0
#define EVENTS_MS 30001

using namespace cv;

#define KMS_NOSE_DETECT_LOCK(nose_detect)				\
  (g_rec_mutex_lock (&( (KmsNoseDetect *) nose_detect)->priv->mutex))

#define KMS_NOSE_DETECT_UNLOCK(nose_detect)				\
  (g_rec_mutex_unlock (&( (KmsNoseDetect *) nose_detect)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_nose_detect_debug_category);
#define GST_CAT_DEFAULT kms_nose_detect_debug_category

#define KMS_NOSE_DETECT_GET_PRIVATE(obj) (				\
					  G_TYPE_INSTANCE_GET_PRIVATE (	\
								       (obj), \
								       KMS_TYPE_NOSE_DETECT, \
								       KmsNoseDetectPrivate \
									) \
									)

enum {
  PROP_0,
  PROP_VIEW_NOSES,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA,
  PROP_MULTI_SCALE_FACTOR,
  PROP_WIDTH_TO_PROCCESS,
  PROP_PROCESS_X_EVERY_4_FRAMES,
  PROP_ACTIVATE_SERVER_EVENTS,
  PROP_SERVER_EVENTS_MS,
  PROP_SHOW_DEBUG_INFO
};



enum {
  SIGNAL_ON_NOSE_EVENT,
  LAST_SIGNAL
};

static guint kms_nose_detector_signals[LAST_SIGNAL] = { 0 };

struct _KmsNoseDetectPrivate {

  IplImage *img_orig;  
  float scale_o2f;//origin 2 face
  float scale_f2n;//face 2 nose
  float scale_n2o;//nose 2 origin 
  int img_width;
  int img_height;
  int width_to_process; 
  int view_noses;
  int num_frames_to_process;
  int detect_event;
  int meta_data;
  int scale_factor;
  int process_x_every_4_frames;
  int num_frame;  
  int server_events;
  int events_ms;
  double time_events_ms;
  GRecMutex mutex;
  gboolean debug;
  GQueue *events_queue;
  GstClockTime pts;
  vector<Rect> *faces;
  vector<Rect> *noses;
  /*detect event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the nose as metadata 
  /*num_frames_to_process*/
  // When we receive an event we need to process at least NUM_FRAMES_TO_PROCESS
};

/* pad templates */

#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsNoseDetect, kms_nose_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_nose_detect_debug_category,
						  PLUGIN_NAME, 0,
						  
						  "debug category for sample_filter element") );

#define MULTI_SCALE_FACTOR(scale) (1 + scale*1.0/100)

static CascadeClassifier fcascade;
static CascadeClassifier ncascade;

template<typename T>
string toString(const T& value)
{
  std::stringstream ss;
   ss << value;
   return ss.str();
}


static int
kms_nose_detect_init_cascade()
{
  if (!fcascade.load(FACE_CONF_FILE) )
    {
      std::cerr << "ERROR: Could not load face cascade" << std::endl;
      return -1;
    }
  
  if (!ncascade.load(NOSE_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load nose cascade" << std::endl;
      return -1;
    }
    
  return 0;
}



static gboolean kms_nose_detect_sink_events(GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;
  KmsNoseDetect *nose = KMS_NOSE_DETECT(trans);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *message;

      GST_OBJECT_LOCK (nose);

      message = gst_structure_copy (gst_event_get_structure (event));

      g_queue_push_tail (nose->priv->events_queue, message);

      GST_OBJECT_UNLOCK (nose);
      break;
    }
  default:
    break;
  }

  ret=  gst_pad_event_default (trans->sinkpad, GST_OBJECT (trans), event);

  return ret;
}

static void kms_nose_send_event(KmsNoseDetect *nose_detect,GstVideoFrame *frame)
{
  GstStructure *face,*nose;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char elem_id[10];
  vector<Rect> *fd=nose_detect->priv->faces;
  vector<Rect> *nd=nose_detect->priv->noses;
  int norm_faces = nose_detect->priv->scale_o2f;
  std::string noses_str;
  struct timeval  end; 
  double current_t, diff_time;

  message= gst_structure_new_empty("noses");

		
  //noses were already normalized on kms_noses_detect_process_frame.
  for(  vector<Rect>::const_iterator m = nd->begin(); m != nd->end(); m++,i++ )
    {
      nose = gst_structure_new("noses",
			       "type", G_TYPE_STRING,"nose", 
			       "x", G_TYPE_UINT,(guint) m->x , 
			       "y", G_TYPE_UINT,(guint) m->y , 
			       "width",G_TYPE_UINT, (guint)m->width ,
			       "height",G_TYPE_UINT, (guint)m->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, nose,NULL);
      gst_structure_free(nose);

           //neccesary info for sending as event to the server
      std::string new_nose ("x:" + toString((guint) m->x ) + 
			    ",y:" + toString((guint) m->y ) + 
			    ",width:" + toString((guint)m->width )+ 
			    ",height:" + toString((guint)m->height )+ ";");
      noses_str= noses_str + new_nose;
    }

  if ((int)nd->size()>0)
    {
      gettimeofday(&end,NULL);
      current_t= ((end.tv_sec * 1000.0) + ((end.tv_usec)/1000.0));
      diff_time = current_t - nose_detect->priv->time_events_ms;

      if (1 == nose_detect->priv->server_events && diff_time > nose_detect->priv->events_ms)
	{
	  nose_detect->priv->time_events_ms=current_t;
	  g_signal_emit (G_OBJECT (nose_detect),
			 kms_nose_detector_signals[SIGNAL_ON_NOSE_EVENT], 0,noses_str.c_str());
	}
      
      /*Adding data as a metadata in the video*/
      /*if (1 == nose_detect->priv->meta_data)    	
	kms_buffer_add_serializable_meta (frame->buffer,message);  	*/      
    }
	
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(nose_detect->base.element.srcpad, event);

}


static void
kms_nose_detect_conf_images (KmsNoseDetect *nose_detect,
                             GstVideoFrame *frame, GstMapInfo &info)
{

  
  nose_detect->priv->img_height = frame->info.height;
  nose_detect->priv->img_width  = frame->info.width;

  if ( ((nose_detect->priv->img_orig != NULL)) &&
       ((nose_detect->priv->img_orig->width != frame->info.width)
	|| (nose_detect->priv->img_orig->height != frame->info.height)) )
    {
      cvReleaseImage(&nose_detect->priv->img_orig);
      nose_detect->priv->img_orig = NULL;
    }

  if (nose_detect->priv->img_orig == NULL)
    
    nose_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							    frame->info.height),
						     IPL_DEPTH_8U, 3);
  
  if (nose_detect->priv->detect_event) 
    /*If we receive faces through event, the coordinates are normalized to the img orig size*/
    nose_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)frame->info.width);
  else 
    nose_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)FACE_WIDTH);
  
  nose_detect->priv->scale_n2o= ((float)frame->info.width) / ((float)nose_detect->priv->width_to_process);
  nose_detect->priv->scale_f2n = ((float)nose_detect->priv->scale_o2f)/((float)nose_detect->priv->scale_n2o);

  nose_detect->priv->img_orig->imageData = (char *) info.data;
}

static void
kms_nose_detect_set_property (GObject *object, guint property_id,
			      const GValue *value, GParamSpec *pspec)
{
  KmsNoseDetect *nose_detect = KMS_NOSE_DETECT (object);

  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  struct timeval  t;
  KMS_NOSE_DETECT_LOCK (nose_detect);

  switch (property_id) {

  case PROP_VIEW_NOSES:
    nose_detect->priv->view_noses = g_value_get_int (value);
    break;
  case PROP_DETECT_BY_EVENT:
    nose_detect->priv->detect_event =  g_value_get_int(value);
    break;
    
  case PROP_SEND_META_DATA:
    nose_detect->priv->meta_data =  g_value_get_int(value);
    break;
    
  case PROP_PROCESS_X_EVERY_4_FRAMES:   
    nose_detect->priv->process_x_every_4_frames = g_value_get_int(value);    
    break;
    
  case PROP_WIDTH_TO_PROCCESS:
    nose_detect->priv->width_to_process = g_value_get_int(value);
    break;
    
  case PROP_MULTI_SCALE_FACTOR:
    nose_detect->priv->scale_factor = g_value_get_int(value);
    break;

  case  PROP_ACTIVATE_SERVER_EVENTS:
    nose_detect->priv->server_events = g_value_get_int(value);
    gettimeofday(&t,NULL);
    nose_detect->priv->time_events_ms= ((t.tv_sec * 1000.0) + ((t.tv_usec)/1000.0));    
    break;
    
  case PROP_SERVER_EVENTS_MS:
    nose_detect->priv->events_ms = g_value_get_int(value);   
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_NOSE_DETECT_UNLOCK (nose_detect);
}

static void
kms_nose_detect_get_property (GObject *object, guint property_id,
			      GValue *value, GParamSpec *pspec)
{
  KmsNoseDetect *nose_detect = KMS_NOSE_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_NOSE_DETECT_LOCK (nose_detect);

  switch (property_id) {


  case PROP_VIEW_NOSES:
    g_value_set_int (value, nose_detect->priv->view_noses);
    break;
    
  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,nose_detect->priv->detect_event);
    break;
    
  case PROP_SEND_META_DATA:
    g_value_set_int(value,nose_detect->priv->meta_data);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    g_value_set_int(value,nose_detect->priv->process_x_every_4_frames);
    break;
    
  case PROP_WIDTH_TO_PROCCESS:
    g_value_set_int(value,nose_detect->priv->width_to_process);
    break;
    
  case PROP_MULTI_SCALE_FACTOR:
    g_value_set_int(value,nose_detect->priv->scale_factor);    
    break;

  case  PROP_ACTIVATE_SERVER_EVENTS:
    g_value_set_int(value,nose_detect->priv->server_events);
    break;
    
  case PROP_SERVER_EVENTS_MS:
    g_value_set_int(value,nose_detect->priv->events_ms);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_NOSE_DETECT_UNLOCK (nose_detect);
}

static gboolean __get_timestamp(KmsNoseDetect *nose,
				GstStructure *message)
{
  GstStructure *ts;
  gboolean ret=false;

  ret = gst_structure_get(message,"timestamp",GST_TYPE_STRUCTURE, &ts,NULL);

  if (ret) {
    
    gst_structure_get(ts,"pts",G_TYPE_UINT64,
		      &nose->priv->pts,NULL);
    gst_structure_free(ts);
  }

  return ret;
}


static bool __get_event_message(KmsNoseDetect *nose,
			  GstStructure *message)
{
  gint len,aux;
  bool result=false;
  int  id=0;
  char struct_id[10];
  const gchar *str_type;
  len = gst_structure_n_fields (message);

  nose->priv->faces->clear();
  FILE *f=fopen("/tmp/nose.log","a");


  for (aux = 0; aux < len; aux++) {
    GstStructure *data;
    gboolean ret;
    const gchar *name = gst_structure_nth_field_name (message, aux);
    
    if (g_strcmp0 (name, "timestamp") == 0) {
      continue;
    }
    //getting the structure
    ret = gst_structure_get (message, name, GST_TYPE_STRUCTURE, &data, NULL);
    if (ret) {
      //type of the structure
      gst_structure_get (data, "type", G_TYPE_STRING, &str_type, NULL);
      
      fprintf(f,"Data received type %s \n", str_type);
      if (g_strcmp0(str_type,FACE_TYPE)==0)//only interested on FACE EVENTS
	{
	  Rect r;
	  gst_structure_get (data, "x", G_TYPE_UINT, & r.x, NULL);
	  gst_structure_get (data, "y", G_TYPE_UINT, & r.y, NULL);
	  gst_structure_get (data, "width", G_TYPE_UINT, & r.width, NULL);
	  gst_structure_get (data, "height", G_TYPE_UINT, & r.height, NULL);	  	 
	  gst_structure_free (data);
	  nose->priv->faces->push_back(r);	      
	  
	}	  
      result=true;
    }  
  }

  fclose(f);
  return result;
}

static bool __receive_event(KmsNoseDetect *nose_detect,GstVideoFrame *frame)
{
  KmsSerializableMeta *metadata;
  gboolean res = false;
  GstStructure *message;
  gboolean ret = false;
  //if detect_event is false it does not matter the event received


  if (0==nose_detect->priv->detect_event) return true;
  
  if (g_queue_get_length(nose_detect->priv->events_queue) == 0) 
    return false;
  
  message= (GstStructure *) g_queue_pop_head(nose_detect->priv->events_queue);
  
  if (NULL != message)
    {
      
      ret=__get_timestamp(nose_detect,message);

      if ( ret )
	{
	  res = __get_event_message(nose_detect,message);	  
	}
    }

  /*metadata = kms_buffer_get_serializable_meta(frame->buffer);

  if (NULL == metadata)   
    return false;

    res = __get_event_message(nose_detect,metadata->data);	  */

  if (res) 
    nose_detect->priv->num_frames_to_process = NUM_FRAMES_TO_PROCESS /
      (5 - nose_detect->priv->process_x_every_4_frames);
  // if we process all the images num_frame_to_process = 10 / 1
  // if we process 3 per 4 images  num_frame_to_process = 10 / 5
  // if we process 2 per 4 images  num_frame_to_process = 10 / 3
  // if we process 1 per 4 images  num_frame_to_process = 10 / 2;


  return res;

}

static vector<Rect> *__merge_noses_consecutives_frames(vector<Rect> *cn, vector<Rect> *noses,
					       Rect &face_cord, int scale)
{
  vector<Rect>::iterator it_n ;
  vector<Rect> *res = new vector<Rect>;

  for (int i=0; i < (int)(noses->size()); i++)
    {
      Point old_center;
      old_center.x = noses->at(i).x + noses->at(i).width/2;
      old_center.y = noses->at(i).y + noses->at(i).height/2;

      for (int j=0; j < (int)(cn->size()); j++)
	{
	  Point new_center;	  
	  new_center.x = (cn->at(j).x + face_cord.x)*scale + ((cn->at(j).width*scale)/2);
	  new_center.y = (cn->at(j).y + face_cord.y)*scale +  ((cn->at(j).height*scale)/2);
	  double h2 = sqrt(pow((new_center.x -old_center.x),2) + 
			   pow((new_center.y - old_center.y),2));	  
	  
	  if (h2 < DEFAULT_EUCLIDEAN_DIS)
	    { /*As the difference among pixels is very low, we mantain the coordinates of the
	       previous nose in order to avoid vibrations*/	      	      
	      res->push_back(noses->at(i));
	      cn->erase(cn->begin()+j);	      
	      break;
	    }
	}      
    }

  if (cn->size() > 0)
    {
      for(it_n = cn->begin(); it_n != cn->end();it_n++)
	{	  
	  /*as it is a new value, we need to take into account the face position to set up
	   the right coordinates*/	  
	  it_n->x = cvRound((face_cord.x + it_n->x)*scale);
	  it_n->y= cvRound((face_cord.y+it_n->y)*scale);
	  it_n->width=(it_n->width-1)*scale;
	  it_n->height=(it_n->height-1)*scale;
	  res->push_back(*it_n);
	}
    }
  
  return res;
}

static void
kms_nose_detect_process_frame(KmsNoseDetect *nose_detect,int width,int height,double scale_f2n,
			      double scale_n2o, double scale_o2f,GstVideoFrame *frame)
{
  Mat img (nose_detect->priv->img_orig);
  vector<Rect> *faces=nose_detect->priv->faces;
  vector<Rect> *noses=nose_detect->priv->noses;
  vector<Rect> *nose = new vector<Rect>;
  vector<Rect> *res= new vector<Rect>;
  Scalar color;
  Mat gray, nose_frame (cvRound(img.rows/scale_n2o), cvRound(img.cols/scale_n2o), CV_8UC1);
  Mat  smallImg( cvRound (img.rows/scale_o2f), cvRound(img.cols/scale_o2f), CV_8UC1 );
  Mat noseROI;
  Rect r_aux;
  int i=0,j=0, k=0;

  const static Scalar colors[] =  { CV_RGB(255,0,255),
				    CV_RGB(255,0,0),
				    CV_RGB(255,255,0),
				    CV_RGB(255,128,0),
				    CV_RGB(0,255,0),
				    CV_RGB(0,255,255),
				    CV_RGB(0,128,255),
				    CV_RGB(0,0,255)} ;


  FILE *f=fopen("/tmp/nose.log","a");
  fprintf(f,"\n ----------------- Iteration -------------------- \n");
  fclose(f);
  if ( ! __receive_event(nose_detect,frame) && nose_detect->priv->num_frames_to_process <=0)
    return;


  nose_detect->priv->num_frame++;

  if ( (2 == nose_detect->priv->process_x_every_4_frames && // one every 2 images
	(1 == nose_detect->priv->num_frame % 2)) ||  
       ( (2 != nose_detect->priv->process_x_every_4_frames) &&
	 (nose_detect->priv->num_frame <= nose_detect->priv->process_x_every_4_frames)))    
    {

      nose_detect->priv->num_frames_to_process --;
      cvtColor( img, gray, CV_BGR2GRAY );

      //if detect_event != 0 we have received faces as meta-data
      if ( 0 == nose_detect->priv->detect_event)
	{
	  //setting up the image where the face detector will be executed
	  resize( gray, smallImg, smallImg.size(), 0, 0, INTER_LINEAR );
	  equalizeHist( smallImg, smallImg );
	  faces->clear();
	  fcascade.detectMultiScale(smallImg,*faces,
				    MULTI_SCALE_FACTOR(nose_detect->priv->scale_factor),2,
				    0 |CV_HAAR_SCALE_IMAGE,
				    Size(3,3));
	}

      //setting up the image e where the nose detector will be executed	
      resize(gray,nose_frame,nose_frame.size(), 0,0,INTER_LINEAR);
      equalizeHist( nose_frame, nose_frame);


      for( vector<Rect>::iterator r = faces->begin(); r != faces->end(); r++,i++ )
	{

	  color = colors[i%8];
	  const int top_height=cvRound((float)r->height*TOP_PERCENTAGE/100);
	  const int down_height=cvRound((float)r->height*DOWN_PERCENTAGE/100);
	  const int side_width=cvRound((float)r->width*SIDE_PERCENTAGE/100);      
	  vector<Rect> *result_aux;

	  //Transforming the point detected in face image to nose coordinates
	  //we only take the down half of the face to avoid excessive processing
	  r_aux.y=(r->y + top_height)*scale_f2n;
	  r_aux.x=(r->x+side_width)*scale_f2n;
	  r_aux.height = (r->height-down_height-top_height)*scale_f2n;
	  r_aux.width = (r->width-side_width)*scale_f2n;
	  noseROI = nose_frame(r_aux);
	  ncascade.detectMultiScale( noseROI, *nose,
				     NOSE_SCALE_FACTOR, 3,
				     0|CV_HAAR_FIND_BIGGEST_OBJECT,
				     Size(1, 1));   

	  if (nose->size()>0)
	    {
	      result_aux=__merge_noses_consecutives_frames(nose,noses,r_aux,scale_n2o);
	  
	      for ( k=0; k < result_aux->size(); k++)		
		res->push_back(result_aux->at(k));		
	    }

	}
    }

  if (nose_detect->priv->noses->size()>0)
    nose_detect->priv->noses->clear();

  for (k=0;k < res->size();k++)
    nose_detect->priv->noses->push_back(res->at(k));

  if (GOP == nose_detect->priv->num_frame )
    nose_detect->priv->num_frame=0;

  //Printing on image
  j=0;
  if (1 == nose_detect->priv->view_noses  )
    for ( vector<Rect>::iterator m = noses->begin(); m != noses->end();m++,j++)	  
      {
	color = colors[j%8];     
	cvRectangle( nose_detect->priv->img_orig, cvPoint(m->x,m->y),
		     cvPoint(cvRound(m->x + m->width), 
			     cvRound(m->y+ m->height-1)),
		     color, 3, 8, 0);	    
      }
  
}
/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_nose_detect_transform_frame_ip (GstVideoFilter *filter,
				    GstVideoFrame *frame)
{
  KmsNoseDetect *nose_detect = KMS_NOSE_DETECT (filter);
  GstMapInfo info;
  double scale_o2f=0.0,scale_n2o=0.0,scale_f2n=0.0;
  int width=0,height=0;

  struct timeval  start,end;

  gettimeofday(&start,NULL);

  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
  // setting up images
  kms_nose_detect_conf_images (nose_detect, frame, info);

  KMS_NOSE_DETECT_LOCK (nose_detect);

  scale_f2n = nose_detect->priv->scale_f2n;
  scale_n2o= nose_detect->priv->scale_n2o;
  scale_o2f = nose_detect->priv->scale_o2f;
  width = nose_detect->priv->img_width;
  height = nose_detect->priv->img_height;
	
  kms_nose_detect_process_frame(nose_detect,width,height,scale_f2n,
				scale_n2o,scale_o2f,frame);

  KMS_NOSE_DETECT_UNLOCK (nose_detect);

  if (1==nose_detect->priv->meta_data)
    kms_nose_send_event(nose_detect,frame);

  gst_buffer_unmap (frame->buffer, &info);

  gettimeofday(&end,NULL);

  unsigned long long time_start= (((float)start.tv_sec * 1000.0) + (float(start.tv_usec)/1000.0));
  unsigned long long time_end=  (((float)end.tv_sec * 1000.0) + (float(end.tv_usec)/1000.0));
  unsigned long long total_time = time_end - time_start;

  return GST_FLOW_OK;
}

/*
 * In dispose(), you are supposed to free all types referenced from this
 * object which might themselves hold a reference to self. Generally,
 * the most simple solution is to unref all members on which you own a
 * reference.
 * dispose() might be called multiple times, so we must guard against
 * calling g_object_unref() on an invalid GObject by setting the member
 * NULL; g_clnose_object() does this for us, atomically.
 */
static void
kms_nose_detect_dispose (GObject *object)
{
}

/*
 * The finalize function is called when the object is destroyed.
 */
static void
kms_nose_detect_finalize (GObject *object)
{
  KmsNoseDetect *nose_detect = KMS_NOSE_DETECT(object);

  cvReleaseImage (&nose_detect->priv->img_orig);
  delete nose_detect->priv->faces;
  delete nose_detect->priv->noses;
  g_rec_mutex_clear(&nose_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_nose_detect_init (KmsNoseDetect *
		      nose_detect)
{
  int ret=0;
  nose_detect->priv = KMS_NOSE_DETECT_GET_PRIVATE (nose_detect);
  nose_detect->priv->img_width = 320;
  nose_detect->priv->img_height = 240;
  nose_detect->priv->img_orig = NULL;
  nose_detect->priv->scale_f2n=1.0;
  nose_detect->priv->scale_n2o=1.0;
  nose_detect->priv->scale_o2f=1.0;
  nose_detect->priv->view_noses=0;
  nose_detect->priv->events_queue= g_queue_new();
  nose_detect->priv->detect_event=0;
  nose_detect->priv->meta_data=0;
  nose_detect->priv->faces= new vector<Rect>;
  nose_detect->priv->noses= new vector<Rect>;
  nose_detect->priv->num_frames_to_process=0;

  nose_detect->priv->process_x_every_4_frames=PROCESS_ALL_FRAMES;
  nose_detect->priv->num_frame=0;
  nose_detect->priv->width_to_process=NOSE_WIDTH;
  nose_detect->priv->scale_factor=DEFAULT_SCALE_FACTOR;
  nose_detect->priv->server_events=SERVER_EVENTS;
  nose_detect->priv->events_ms=EVENTS_MS;

  if (fcascade.empty())
    if (kms_nose_detect_init_cascade() < 0)
      std::cout << "Error: init cascade" << std::endl;


  if (ret != 0)
    GST_DEBUG ("Error reading the haar cascade configuration file");

  g_rec_mutex_init(&nose_detect->priv->mutex);

}

static void
kms_nose_detect_class_init (KmsNoseDetectClass *nose)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (nose);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (nose);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (nose),
				      gst_pad_template_new ("src", GST_PAD_SRC,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (nose),
				      gst_pad_template_new ("sink", GST_PAD_SINK,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (nose),
					 "nose detection filter element", "Video/Filter",
					 "Nose detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_nose_detect_set_property;
  gobject_class->get_property = kms_nose_detect_get_property;
  gobject_class->dispose = kms_nose_detect_dispose;
  gobject_class->finalize = kms_nose_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_NOSES,
				   g_param_spec_int ("view-noses", "view noses",
						     "To indicate whether or not we have to draw  the detected nose on the stream",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  
  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event like face detection",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("send-meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the nose and face", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

g_object_class_install_property (gobject_class, PROP_WIDTH_TO_PROCCESS,
				   g_param_spec_int ("width-to-process", "width to process",
						     "160,320 (default),480,640 => this will be the width of the image that the algorithm is going to process to detect noses", 
						     0,640,FALSE, (GParamFlags) G_PARAM_READWRITE));

 g_object_class_install_property (gobject_class,   PROP_PROCESS_X_EVERY_4_FRAMES,
				  g_param_spec_int ("process-x-every-4-frames", "process x every 4 frames",
						    "1,2,3,4 (default) => process x frames every 4 frames", 
						    0,4,FALSE, (GParamFlags) G_PARAM_READWRITE));
 

 g_object_class_install_property (gobject_class,   PROP_MULTI_SCALE_FACTOR,
				  g_param_spec_int ("multi-scale-factor", "multi scale factor",
						    "5-50  (25 default) => specifying how much the image size is reduced at each image scale.", 
						    0,51,FALSE, (GParamFlags) G_PARAM_READWRITE));
	  

 g_object_class_install_property (gobject_class,   PROP_ACTIVATE_SERVER_EVENTS,
				  g_param_spec_int ("activate-events", "Activate Events",
						    "(0 default) => It will not send events to server, 1 => it will send events to the server", 
			          0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

 g_object_class_install_property (gobject_class,   PROP_SERVER_EVENTS_MS,
				  g_param_spec_int ("events-ms",  "Activate Events",
						    "the time, it takes to send events to the servers", 
			          0,30000,FALSE, (GParamFlags) G_PARAM_READWRITE));

  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_nose_detect_transform_frame_ip);

  kms_nose_detector_signals[SIGNAL_ON_NOSE_EVENT] =
    g_signal_new ("nose-event",
		  G_TYPE_FROM_CLASS (nose),
		  G_SIGNAL_RUN_LAST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 1, G_TYPE_STRING);

  /*Properties initialization*/
  g_type_class_add_private (nose, sizeof (KmsNoseDetectPrivate) );
  
  nose->base_nose_detect_class.parent_class.sink_event =
    GST_DEBUG_FUNCPTR(kms_nose_detect_sink_events);

}

gboolean
kms_nose_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_NOSE_DETECT);
}
