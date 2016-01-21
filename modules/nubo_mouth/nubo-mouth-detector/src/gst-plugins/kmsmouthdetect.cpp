 #include "kmsmouthdetect.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>
#include <opencv2/opencv.hpp>


#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#define PLUGIN_NAME "nubomouthdetector"
#define FACE_WIDTH 160
#define MOUTH_WIDTH 320
#define DEFAULT_FILTER_TYPE (KmsMouthDetectType)0
#define NUM_FRAMES_TO_PROCESS 10
#define FACE_TYPE "face"
#define DEFAULT_EUCLIDEAN_DIS 4

#define PROCESS_ALL_FRAMES 4
#define GOP 4
#define DEFAULT_SCALE_FACTOR 25
#define MOUTH_SCALE_FACTOR 1.1

#define FACE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_alt.xml"
#define MOUTH_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_mouth.xml"

using namespace cv;

#define KMS_MOUTH_DETECT_LOCK(mouth_detect)				\
  (g_rec_mutex_lock (&( (KmsMouthDetect *) mouth_detect)->priv->mutex))

#define KMS_MOUTH_DETECT_UNLOCK(mouth_detect)				\
  (g_rec_mutex_unlock (&( (KmsMouthDetect *) mouth_detect)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_mouth_detect_debug_category);
#define GST_CAT_DEFAULT kms_mouth_detect_debug_category

#define KMS_MOUTH_DETECT_GET_PRIVATE(obj) (				\
					   G_TYPE_INSTANCE_GET_PRIVATE ( \
									(obj), \
									KMS_TYPE_MOUTH_DETECT, \
									KmsMouthDetectPrivate \
										) \
					       )
enum {
  PROP_0,
  PROP_VIEW_MOUTHS,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA,
  PROP_MULTI_SCALE_FACTOR,
  PROP_WIDTH_TO_PROCESS,
  PROP_PROCESS_X_EVERY_4_FRAMES
};

struct _KmsMouthDetectPrivate {

  IplImage *img_orig;
  
  int img_width;
  int img_height;
  int view_mouths;
  int num_frames_to_process;
  int detect_event;
  int meta_data;
  int width_to_process; 
  int process_x_every_4_frames;
  int scale_factor;
  int num_frame;

  float scale_o2f;//origin 2 face
  float scale_f2m;//face 2 mouth
  float scale_m2o;//mounth 2 origin 

  GRecMutex mutex;
  gboolean debug;
  GQueue *events_queue;
  GstClockTime pts;

  vector<Rect> *faces;
  vector<Rect> *mouths;
  /*detect event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the mouth as metadata 
  /*num_frames_to_process*/
  // When we receive an event we need to process at least NUM_FRAMES_TO_PROCESS
};

/* pad templates */

#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsMouthDetect, kms_mouth_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_mouth_detect_debug_category,
						  PLUGIN_NAME, 0,
						  
						  "debug category for sample_filter element") );

#define MULTI_SCALE_FACTOR(scale) (1 + scale*1.0/100)

static CascadeClassifier fcascade;
static CascadeClassifier mcascade;

static int
kms_mouth_detect_init_cascade()
{
  if (!fcascade.load(FACE_CONF_FILE) )
    {
      std::cerr << "ERROR: Could not load face cascade" << std::endl;
      return -1;
    }
  
  if (!mcascade.load(MOUTH_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load mouth cascade" << std::endl;
      return -1;
    }
  
  
  return 0;
}


static gboolean kms_mouth_detect_sink_events(GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;
  KmsMouthDetect *mouth = KMS_MOUTH_DETECT(trans);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *message;

      GST_OBJECT_LOCK (mouth);

      message = gst_structure_copy (gst_event_get_structure (event));

      g_queue_push_tail (mouth->priv->events_queue, message);

      GST_OBJECT_UNLOCK (mouth);
      break;
    }
  default:
    break;
  }
  ret=  gst_pad_event_default (trans->sinkpad, GST_OBJECT (trans), event);

  return ret;
}


static void kms_mouth_send_event(KmsMouthDetect *mouth_detect,GstVideoFrame *frame)
{
  GstStructure *face,*mouth;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char elem_id[10];
  vector<Rect> *fd=mouth_detect->priv->faces;
  vector<Rect> *md=mouth_detect->priv->mouths;
  int norm_faces = mouth_detect->priv->scale_o2f;

  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);
	
  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
		
  for(  vector<Rect>::const_iterator r = fd->begin(); r != fd->end(); r++,i++ )
    {
      face = gst_structure_new("face",
			       "type", G_TYPE_STRING,"face", 
			       "x", G_TYPE_UINT,(guint) r->x * norm_faces, 
			       "y", G_TYPE_UINT,(guint) r->y * norm_faces, 
			       "width",G_TYPE_UINT, (guint)r->width * norm_faces,
			       "height",G_TYPE_UINT, (guint)r->height * norm_faces,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, face,NULL);
      gst_structure_free(face);
    }
  

  //mouths were already normalized on kms_mouth_detect_process_frame.
  for(  vector<Rect>::const_iterator m = md->begin(); m != md->end(); m++,i++ )
    {
      mouth = gst_structure_new("mouth",
			       "type", G_TYPE_STRING,"mouth", 
			       "x", G_TYPE_UINT,(guint) m->x , 
			       "y", G_TYPE_UINT,(guint) m->y , 
			       "width",G_TYPE_UINT, (guint)m->width, 
			       "height",G_TYPE_UINT, (guint)m->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, mouth,NULL);
      gst_structure_free(mouth);
    }
  /*post a faces detected event to src pad*/
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(mouth_detect->base.element.srcpad, event);
	
}

static void
kms_mouth_detect_conf_images (KmsMouthDetect *mouth_detect,
			      GstVideoFrame *frame, GstMapInfo &info)
{

  mouth_detect->priv->img_height = frame->info.height;
  mouth_detect->priv->img_width  = frame->info.width;

  if ( ((mouth_detect->priv->img_orig != NULL)) &&
       ((mouth_detect->priv->img_orig->width != frame->info.width)
	|| (mouth_detect->priv->img_orig->height != frame->info.height)))
    {
      cvReleaseImage(&mouth_detect->priv->img_orig);
      mouth_detect->priv->img_orig = NULL;
    }

  if (mouth_detect->priv->img_orig == NULL)
    mouth_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							     frame->info.height),
						      IPL_DEPTH_8U, 3);
  if (mouth_detect->priv->detect_event) 
    /*If we receive faces through event, the coordinates are normalized to the img orig size*/
    mouth_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)frame->info.width);
  else 
    mouth_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)FACE_WIDTH);

  mouth_detect->priv->scale_m2o= ((float) frame->info.width) / ((float)mouth_detect->priv->width_to_process);
  mouth_detect->priv->scale_f2m = ((float)mouth_detect->priv->scale_o2f) / ((float)mouth_detect->priv->scale_m2o);
  mouth_detect->priv->img_orig->imageData = (char *) info.data;

}

static void
kms_mouth_detect_set_property (GObject *object, guint property_id,
			       const GValue *value, GParamSpec *pspec)
{
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT (object);
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_MOUTH_DETECT_LOCK (mouth_detect);

  switch (property_id) {
  
  case PROP_VIEW_MOUTHS:
    mouth_detect->priv->view_mouths = g_value_get_int (value);
    break;
    
  case PROP_DETECT_BY_EVENT:
    mouth_detect->priv->detect_event =  g_value_get_int(value);
    break;

  case PROP_SEND_META_DATA:
    mouth_detect->priv->meta_data =  g_value_get_int(value);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    mouth_detect->priv->process_x_every_4_frames = g_value_get_int(value);
    break;

  case PROP_MULTI_SCALE_FACTOR:
    mouth_detect->priv->scale_factor = g_value_get_int(value);
    break;

  case PROP_WIDTH_TO_PROCESS:
    mouth_detect->priv->width_to_process = g_value_get_int(value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_MOUTH_DETECT_UNLOCK (mouth_detect);

}

static void
kms_mouth_detect_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_MOUTH_DETECT_LOCK (mouth_detect);

  switch (property_id) {

  case PROP_VIEW_MOUTHS:
    g_value_set_int (value, mouth_detect->priv->view_mouths);
    break;

  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,mouth_detect->priv->detect_event);
    break;
    
  case PROP_SEND_META_DATA:
    g_value_set_int(value,mouth_detect->priv->meta_data);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    g_value_set_int(value,mouth_detect->priv->process_x_every_4_frames);
    break;

  case PROP_MULTI_SCALE_FACTOR:
    g_value_set_int(value,mouth_detect->priv->scale_factor);
    break;

  case PROP_WIDTH_TO_PROCESS:
    g_value_set_int(value,mouth_detect->priv->width_to_process);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_MOUTH_DETECT_UNLOCK (mouth_detect);
}

static gboolean __get_timestamp(KmsMouthDetect *mouth,
				GstStructure *message)
{
  GstStructure *ts;
  gboolean ret=false;

  ret = gst_structure_get(message,"timestamp",GST_TYPE_STRUCTURE, &ts,NULL);

  if (ret) {
    
    gst_structure_get(ts,"pts",G_TYPE_UINT64,
		      &mouth->priv->pts,NULL);
    gst_structure_free(ts);
  }

  return ret;
}

static bool __get_message(KmsMouthDetect *mouth,
			  GstStructure *message)
{
  gint len,aux;
  bool result=false;
  //int movement=-1;
  int  id=0;
  char struct_id[10];
  const gchar *str_type;

  len = gst_structure_n_fields (message);

  mouth->priv->faces->clear();

  for (aux = 0; aux < len; aux++) {
    GstStructure *data;
    gboolean ret;

    const gchar *name = gst_structure_nth_field_name (message, aux);
    
    if (g_strcmp0 (name, "timestamp") == 0) {
      continue;
    }

    sprintf(struct_id,"%d",id);    
    if ((g_strcmp0(name,struct_id) == 0)) //get structure's id
      {
	id++;
	//getting the structure
	ret = gst_structure_get (message, name, GST_TYPE_STRUCTURE, &data, NULL);
	if (ret) {
	  //type of the structure
	  gst_structure_get (data, "type", G_TYPE_STRING, &str_type, NULL);
	  if (g_strcmp0(str_type,FACE_TYPE)==0)
	    {
	      Rect r;
	      gst_structure_get (data, "x", G_TYPE_UINT, & r.x, NULL);
	      gst_structure_get (data, "y", G_TYPE_UINT, & r.y, NULL);
	      gst_structure_get (data, "width", G_TYPE_UINT, & r.width, NULL);
	      gst_structure_get (data, "height", G_TYPE_UINT, & r.height, NULL);	  	 
	      gst_structure_free (data);
	      mouth->priv->faces->push_back(r);
	    }	  
	  result=true;
	}
      }
  }            
  return result;
}

static bool __process_alg(KmsMouthDetect *mouth_detect, GstClockTime f_pts)
{
  GstStructure *message;
  bool res=false;
  gboolean ret = false;
  //if detect_event is false it does not matter the event received

  if (0==mouth_detect->priv->detect_event) return true;

  if (g_queue_get_length(mouth_detect->priv->events_queue) == 0) 
    return false;
	
  message= (GstStructure *) g_queue_pop_head(mouth_detect->priv->events_queue);

  if (NULL != message)
    {

      ret=__get_timestamp(mouth_detect,message);
       //if ( ret && mouth_detect->priv->pts == f_pts)
      if ( ret )
	{
	  res = __get_message(mouth_detect,message);	  
	}
    }


  if (res) 
    mouth_detect->priv->num_frames_to_process = NUM_FRAMES_TO_PROCESS / 
      (5 - mouth_detect->priv->process_x_every_4_frames);
  // if we process all the images num_frame_to_process = 10 / 1
  // if we process 3 per 4 images  num_frame_to_process = 10 / 5
  // if we process 2 per 4 images  num_frame_to_process = 10 / 3
  // if we process 1 per 4 images  num_frame_to_process = 10 / 2
  
  return res;
}

static vector<Rect> *__merge_mouths_consecutives_frames(vector<Rect> *cm, vector<Rect> *mouths,
					       Rect &face_cord, int scale)
{
  vector<Rect>::iterator it_m ;
  FILE *f;
  vector<Rect> *res = new vector<Rect>;

  res->clear();

  for (int i=0; i < (int)(mouths->size()); i++)
    {
      Point old_center;
      old_center.x = mouths->at(i).x + mouths->at(i).width/2;
      old_center.y = mouths->at(i).y + mouths->at(i).height/2;

      for (int j=0; j < (int)(cm->size()); j++)
	{
	  Point new_center;	  
	  new_center.x = (cm->at(j).x + face_cord.x)*scale + ((cm->at(j).width * scale)/2);
	  new_center.y = (cm->at(j).y + face_cord.y)*scale +  ((cm->at(j).height*scale)/2);
	  double h2 = sqrt(pow((new_center.x -old_center.x),2) + 
			   pow((new_center.y - old_center.y),2));	  
	  if (h2 < DEFAULT_EUCLIDEAN_DIS)
	    { /*As the difference among pixels is very low, we mantain the coordinates of the
	       previous mouth in order to avoid vibrations*/	      
	      res->push_back(mouths->at(i));
	      cm->erase(cm->begin()+j);	      
	      break;
	    }
	}      
    }

  if (cm->size() > 0)
    {
      for(it_m = cm->begin(); it_m != cm->end();it_m++)
	{	  
	  /*as it is a new value, we need to take into account the face position to set up
	   the right coordinates*/	  
	  it_m->x = cvRound((face_cord.x + it_m->x)*scale);
	  it_m->y= cvRound((face_cord.y+it_m->y)*scale);
	  it_m->width=(it_m->width-1)*scale;
	  it_m->height=(it_m->height-1)*scale;
	  res->push_back(*it_m);
	}
    }
  
  return res;
}
  
static void
kms_mouth_detect_process_frame(KmsMouthDetect *mouth_detect,int width,int height,
			       double scale_f2m,double scale_m2o, double scale_o2f,GstClockTime pts)
{
  int i = 0;
  Scalar color;
  Mat img (mouth_detect->priv->img_orig);
  Mat gray, mouth_frame (cvRound(img.rows/scale_m2o), cvRound(img.cols/scale_m2o), CV_8UC1);
  Mat  smallImg( cvRound (img.rows/scale_o2f), cvRound(img.cols/scale_o2f), CV_8UC1 );
  Mat mouthROI;
  vector<Rect> *faces=mouth_detect->priv->faces;
  vector<Rect> *mouths =mouth_detect->priv->mouths;
  vector<Rect> *mouth= new vector<Rect>;
  vector<Rect> *res= new vector<Rect>;
  int k=0;
  Rect r_aux;
  FILE *f;
  const static Scalar colors[] =  { CV_RGB(255,255,0),
				    CV_RGB(255,128,0),
				    CV_RGB(255,0,0),
				    CV_RGB(255,0,255),
				    CV_RGB(0,128,255),
				    CV_RGB(0,0,255),
				    CV_RGB(0,255,255),
				    CV_RGB(0,255,0)};	

  
  if ( ! __process_alg(mouth_detect,pts) && mouth_detect->priv->num_frames_to_process <=0)
    return;

  mouth_detect->priv->num_frame++;

  if ( (2 == mouth_detect->priv->process_x_every_4_frames && // one every 2 images
	(1 == mouth_detect->priv->num_frame % 2)) ||  
       ( (2 != mouth_detect->priv->process_x_every_4_frames) &&
	 (mouth_detect->priv->num_frame <= mouth_detect->priv->process_x_every_4_frames)))    
    {

      mouth_detect->priv->num_frames_to_process --;
      cvtColor( img, gray, CV_BGR2GRAY );

      //if detect_event != 0 we have received faces as meta-data
      if (0 == mouth_detect->priv->detect_event )
	{ //detecting faces
	  //setting up the image where the face detector will be executed
	  resize( gray, smallImg, smallImg.size(), 0, 0, INTER_LINEAR );
	  equalizeHist( smallImg, smallImg );
	  faces->clear();
	  fcascade.detectMultiScale( smallImg, *faces,
				     MULTI_SCALE_FACTOR(mouth_detect->priv->scale_factor), 2, 
				     0 | CV_HAAR_SCALE_IMAGE,
				     Size(3, 3) );
	}
      
      //setting up the image where the mouth detector will be executed	
      resize(gray,mouth_frame,mouth_frame.size(), 0,0,INTER_LINEAR);
      equalizeHist( mouth_frame, mouth_frame);
          

      for( vector<Rect>::iterator r = faces->begin(); r != faces->end(); r++, i++ )
	{	
	  vector<Rect> *result_aux;
	  const int half_height=cvRound((float)r->height/1.8);
	  //Transforming the point detected in face image to mouht coordinates
	  //we only take the down half of the face to avoid excessive processing	  
	  r_aux.y=(r->y + half_height)*scale_f2m;
	  r_aux.x=r->x*scale_f2m;
	  r_aux.height = half_height*scale_f2m;
	  r_aux.width = r->width*scale_f2m;
	  
	  mouthROI = mouth_frame(r_aux);
	  /*In this case, the scale factor is fixed, values higher than 1.1 work much worse*/

	  mcascade.detectMultiScale( mouthROI, *mouth,
				     MOUTH_SCALE_FACTOR, 3, 0
				     |CV_HAAR_FIND_BIGGEST_OBJECT,
				     Size(1,1));
	  if (mouth->size()>0)	    
	    {
	      result_aux=__merge_mouths_consecutives_frames(mouth,mouths,r_aux, scale_m2o);
	      for ( k=0; k < result_aux->size(); k++)		
		  res->push_back(result_aux->at(k));		
	    }
	}	  
    }

  if (mouth_detect->priv->mouths->size()>0)
    mouth_detect->priv->mouths->clear();

  for (k=0;k < res->size();k++)
    mouth_detect->priv->mouths->push_back(res->at(k));

  if (GOP == mouth_detect->priv->num_frame )
    mouth_detect->priv->num_frame=0;
  
  //Printing on image
  int j=0;

  if (1 == mouth_detect->priv->view_mouths)
    for ( vector<Rect>::iterator m = mouths->begin(); m != mouths->end();m++,j++)	  
      {
	color = colors[j%8];     
	cvRectangle( mouth_detect->priv->img_orig, cvPoint(m->x,m->y),
		     cvPoint(cvRound(m->x + m->width-1), 
			     cvRound(m->y + m->height-1)),
		     color, 3, 8, 0);	    
      }
}
/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_mouth_detect_transform_frame_ip (GstVideoFilter *filter,
				     GstVideoFrame *frame)
{
      
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT (filter);
  GstMapInfo info;
  double scale_o2f=0.0,scale_m2o=0.0,scale_f2m=0.0;
  int width=0,height=0;

  struct timeval  start,end;

  gettimeofday(&start,NULL);
  
  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
  kms_mouth_detect_conf_images (mouth_detect, frame, info);  // setting up images
      
  KMS_MOUTH_DETECT_LOCK (mouth_detect);
      
  scale_f2m = mouth_detect->priv->scale_f2m;
  scale_m2o= mouth_detect->priv->scale_m2o;
  scale_o2f = mouth_detect->priv->scale_o2f;
  width = mouth_detect->priv->img_width;
  height = mouth_detect->priv->img_height;
      
  KMS_MOUTH_DETECT_UNLOCK (mouth_detect);
    
  kms_mouth_detect_process_frame(mouth_detect,width,height,scale_f2m,
				 scale_m2o,scale_o2f,frame->buffer->pts);
	
  if (1==mouth_detect->priv->meta_data)
    kms_mouth_send_event(mouth_detect,frame);
     
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
 * NULL; g_clmouth_object() does this for us, atomically.
 */
static void
kms_mouth_detect_dispose (GObject *object)
{
}

/*
 * The finalize function is called when the object is destroyed.
 */
static void
kms_mouth_detect_finalize (GObject *object)
{
  KmsMouthDetect *mouth_detect = KMS_MOUTH_DETECT(object);

  cvReleaseImage (&mouth_detect->priv->img_orig);
  delete mouth_detect->priv->faces;
  delete mouth_detect->priv->mouths;
  g_rec_mutex_clear(&mouth_detect->priv->mutex);
}
/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_mouth_detect_init (KmsMouthDetect *
		       mouth_detect)
{
  std::cout << "En mouth detect init" << std::endl;
  int ret=0;
  mouth_detect->priv = KMS_MOUTH_DETECT_GET_PRIVATE (mouth_detect);
  mouth_detect->priv->scale_f2m=1.0;
  mouth_detect->priv->scale_m2o=1.0;
  mouth_detect->priv->scale_o2f=1.0;
  mouth_detect->priv->img_width = 320;
  mouth_detect->priv->img_height = 240;
  mouth_detect->priv->img_orig = NULL;
  mouth_detect->priv->view_mouths=0;
  mouth_detect->priv->events_queue= g_queue_new();
  mouth_detect->priv->detect_event=0;
  mouth_detect->priv->meta_data=0;
  mouth_detect->priv->faces= new vector<Rect>;
  mouth_detect->priv->mouths= new vector<Rect>;
  mouth_detect->priv->num_frames_to_process=0;

  mouth_detect->priv->process_x_every_4_frames=PROCESS_ALL_FRAMES;
  mouth_detect->priv->num_frame=0;
  mouth_detect->priv->scale_factor=DEFAULT_SCALE_FACTOR;
  mouth_detect->priv->width_to_process=MOUTH_WIDTH;

  if (fcascade.empty())
    if (kms_mouth_detect_init_cascade() < 0)
      std::cout << "Error: init cascade" << std::endl;

  if (ret != 0)
    GST_DEBUG ("Error reading the haar cascade configuration file");

  g_rec_mutex_init(&mouth_detect->priv->mutex);

  std::cout << "En mouth detect init END" << std::endl;
}

static void
kms_mouth_detect_class_init (KmsMouthDetectClass *mouth)
{
    
  GObjectClass *gobject_class = G_OBJECT_CLASS (mouth);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (mouth);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (mouth),
				      gst_pad_template_new ("src", GST_PAD_SRC,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (mouth),
				      gst_pad_template_new ("sink", GST_PAD_SINK,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (mouth),
					 "mouth detection filter element", "Video/Filter",
					 "Fade detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_mouth_detect_set_property;
  gobject_class->get_property = kms_mouth_detect_get_property;
  gobject_class->dispose = kms_mouth_detect_dispose;
  gobject_class->finalize = kms_mouth_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_MOUTHS,
				   g_param_spec_int ("view-mouths", "view mouths",
						     "To indicate whether or not we have to draw  the detected mouths on the stream ",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE) );

  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event like face detection",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the mouth and face", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_WIDTH_TO_PROCESS,
				   g_param_spec_int ("width-to-process", "width to process",
						     "160,320 (default),480,640 => this will be the width of the image that the algorithm is going to process to detect mouths", 
						     0,640,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,   PROP_PROCESS_X_EVERY_4_FRAMES,
				   g_param_spec_int ("process-x-every-4-frames", "process x every 4 frames",
						     "1,2,3,4 (default) => process x frames every 4 frames", 
						     0,4,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,   PROP_MULTI_SCALE_FACTOR,
				   g_param_spec_int ("multi-scale-factor", "multi scale factor",
						     "5-50  (25 default) => specifying how much the image size is reduced at each image scale.", 
						     0,51,FALSE, (GParamFlags) G_PARAM_READWRITE));

  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_mouth_detect_transform_frame_ip);

  /*Properties initialization*/
  g_type_class_add_private (mouth, sizeof (KmsMouthDetectPrivate) );

  mouth->base_mouth_detect_class.parent_class.sink_event =
    GST_DEBUG_FUNCPTR(kms_mouth_detect_sink_events);
}

gboolean
kms_mouth_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_MOUTH_DETECT);
}
