#include "kmseardetect.h"
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

#include <libsoup/soup.h>
#include <ftw.h>

#define PLUGIN_NAME "nuboeardetector"
#define FACE_WIDTH 160
#define EAR_WIDTH 320
#define DEFAULT_FILTER_TYPE (KmsEarDetectType)0

#define FACE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_profileface.xml"
#define REAR_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_leftear.xml"
#define LEAR_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_rightear.xml"

#define LEFT_SIDE 	0
#define RIGHT_SIDE 	1
#define FACE_STEP 	0
#define EAR_STEP 	1

#define TOP_PERCENTAGE 20
#define DOWN_PERCENTAGE 20

#define PROCESS_ALL_FRAMES 4
#define GOP 4
#define DEFAULT_SCALE_FACTOR 25
#define EAR_SCALE_FACTOR 1.1
#define NUM_FRAMES_TO_PROCESS 10
#define SERVER_EVENTS 0
#define EVENTS_MS 30001
#define MAX_NUM_FPS_WITH_NO_DETECTION 4

//number of pixels to widen the face area
#define EXTRA_ROI	50
#define TEMP_PATH "/tmp/XXXXXX"
#define SRC_OVERLAY ((double)1)
using namespace cv;

#define KMS_EAR_DETECT_LOCK(ear_detect)					\
  (g_rec_mutex_lock (&( (KmsEarDetect *) ear_detect)->priv->mutex))

#define KMS_EAR_DETECT_UNLOCK(ear_detect)				\
  (g_rec_mutex_unlock (&( (KmsEarDetect *) ear_detect)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_ear_detect_debug_category);	
#define GST_CAT_DEFAULT kms_ear_detect_debug_category

#define KMS_EAR_DETECT_GET_PRIVATE(obj) (				\
					 G_TYPE_INSTANCE_GET_PRIVATE (	\
								      (obj), \
								      KMS_TYPE_EAR_DETECT, \
								      KmsEarDetectPrivate \
									) \
					     )
enum {
  PROP_0,
  PROP_VIEW_EARS,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA,
  PROP_MULTI_SCALE_FACTOR,
  PROP_WIDTH_TO_PROCESS,
  PROP_PROCESS_X_EVERY_4_FRAMES,
  PROP_ACTIVATE_SERVER_EVENTS,
  PROP_SERVER_EVENTS_MS,
  PROP_IMAGE_TO_OVERLAY
};

enum {
  SIGNAL_ON_EAR_EVENT,
  LAST_SIGNAL
};

static guint kms_ear_detector_signals[LAST_SIGNAL] = { 0 };

struct _KmsEarDetectPrivate {

  IplImage *img_orig;
    
  int img_width;
  int img_height;
  int view_ears;
  int show_faces;
  int discard;
  int process_x_every_4_frames;
  int detect_event;
  int meta_data;
  int width_to_process; 
  int scale_factor;
  int num_frame;
  int num_frames_to_process;
  int server_events;
  int events_ms;
  double time_events_ms;
  float scale_f2o;//origin 2 face
  float scale_f2e;//face 2 ear
  float scale_e2o;//mounth 2 origin 
  
  GRecMutex mutex;
  gboolean debug;
  int frames_with_no_detection;

  vector<Rect> *faces;
  vector<Rect> *ears;
  vector<Rect> *rear;
  vector<Rect> *lear;

  /*detect event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the ears as metadata 
  GstStructure *image_to_overlay;
  gdouble offsetXPercent, offsetYPercent, widthPercent, heightPercent;
  IplImage *costume;
  gboolean dir_created;
  gchar *dir;
};

/* pad templates */
#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsEarDetect, kms_ear_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_ear_detect_debug_category,
						  PLUGIN_NAME, 0,
						  "debug category for sample_filter element") );

#define MULTI_SCALE_FACTOR(scale) (1 + scale*1.0/100)

static CascadeClassifier fcascade;
static CascadeClassifier lecascade;
static CascadeClassifier recascade;

template<typename T>
string toString(const T& value)
{
  std::stringstream ss;
   ss << value;
   return ss.str();
}

static int
kms_ear_detect_init_cascade()
{
  if (!fcascade.load(FACE_CONF_FILE) )
    {
      std::cerr << "ERROR: Could not load face cascade" << std::endl;
      return -1;
    }
  
  if (!lecascade.load(LEAR_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load ear cascade" << std::endl;
      return -1;
    }

  
  if (!recascade.load(REAR_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load ear cascade" << std::endl;
      return -1;
    }

  return 0;
}

static void kms_ear_send_event(KmsEarDetect *ear_detect,GstVideoFrame *frame)
{
  GstStructure *face,*ear;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char elem_id[10];
  vector<Rect> *fd=ear_detect->priv->faces;
  vector<Rect> *er= ear_detect->priv->rear;    
  vector<Rect> *el= ear_detect->priv->lear;    
  std::string ears_str;
  struct timeval  end; 
  double current_t, diff_time;

  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);
	
  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
		
  for(  vector<Rect>::const_iterator r = fd->begin(); r != fd->end(); r++,i++ )
    {
      face = gst_structure_new("face_profile",
			       "type", G_TYPE_STRING,"face_profile", 
			       "x", G_TYPE_UINT,(guint) r->x, 
			       "y", G_TYPE_UINT,(guint) r->y, 
			       "width",G_TYPE_UINT, (guint)r->width,
			       "height",G_TYPE_UINT, (guint)r->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, face,NULL);
      gst_structure_free(face);
    }
  
  for(  vector<Rect>::const_iterator m = er->begin(); m != er->end(); m++,i++ )
    {
      ear = gst_structure_new("ear",
			       "type", G_TYPE_STRING,"ear", 
			       "x", G_TYPE_UINT,(guint) m->x, 
			       "y", G_TYPE_UINT,(guint) m->y, 
			       "width",G_TYPE_UINT, (guint)m->width,
			       "height",G_TYPE_UINT, (guint)m->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, ear,NULL);
      gst_structure_free(ear);
      std::string new_ear ("x:" + toString((guint) m->x ) + 
			    ",y:" + toString((guint) m->y ) + 
			    ",width:" + toString((guint)m->width )+ 
			    ",height:" + toString((guint)m->height )+ ";");
      ears_str= ears_str + new_ear;
    }

  for(  vector<Rect>::const_iterator m = el->begin(); m != el->end(); m++,i++ )
    {
      ear = gst_structure_new("ear",
			       "type", G_TYPE_STRING,"ear", 
			       "x", G_TYPE_UINT,(guint) m->x, 
			       "y", G_TYPE_UINT,(guint) m->y, 
			       "width",G_TYPE_UINT, (guint)m->width,
			       "height",G_TYPE_UINT, (guint)m->height,
			       NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, ear,NULL);
      gst_structure_free(ear);

      std::string new_ear ("x:" + toString((guint) m->x ) + 
			   ",y:" + toString((guint) m->y ) + 
			   ",width:" + toString((guint)m->width )+ 
			   ",height:" + toString((guint)m->height )+ ";");
      ears_str= ears_str + new_ear;
    }

    if ((int)el->size()>0 || (int)er->size()>0)
    {

      gettimeofday(&end,NULL);
      current_t= ((end.tv_sec * 1000.0) + ((end.tv_usec)/1000.0));
      diff_time = current_t - ear_detect->priv->time_events_ms;

      if (1 == ear_detect->priv->server_events && diff_time > ear_detect->priv->events_ms)
	{
	  ear_detect->priv->time_events_ms=current_t;
	  g_signal_emit (G_OBJECT (ear_detect),
			 kms_ear_detector_signals[SIGNAL_ON_EAR_EVENT], 0,ears_str.c_str());
	}
      
      /*info about the ear detected added as a metada*/
      /*if (1 == ear_detect->priv->meta_data)    
	kms_buffer_add_serializable_meta (frame->buffer,message);  */

    }
	
}

static void
kms_ear_detect_conf_images (KmsEarDetect *ear_detect,
			    GstVideoFrame *frame, GstMapInfo &info)
{

  ear_detect->priv->img_height = frame->info.height;
  ear_detect->priv->img_width  = frame->info.width;

  if ( ((ear_detect->priv->img_orig != NULL)) &&
       ((ear_detect->priv->img_orig->width != frame->info.width)
	|| (ear_detect->priv->img_orig->height != frame->info.height)) )
    {
      cvReleaseImage(&ear_detect->priv->img_orig);
      ear_detect->priv->img_orig = NULL;
    }

  if (ear_detect->priv->img_orig == NULL)

    ear_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							   frame->info.height),
						    IPL_DEPTH_8U, 3);

  ear_detect->priv->scale_f2o = ((float)frame->info.width) / ((float)FACE_WIDTH);
  ear_detect->priv->scale_e2o = ((float)frame->info.width) / ((float)ear_detect->priv->width_to_process);
  ear_detect->priv->scale_f2e = ((float)ear_detect->priv->scale_f2o)/((float)ear_detect->priv->scale_e2o);
  
  ear_detect->priv->img_orig->imageData = (char *) info.data;
}

static gboolean
is_valid_uri (const gchar * url)
{
  gboolean ret;
  GRegex *regex;

  regex = g_regex_new ("^(?:((?:https?):)\\/\\/)([^:\\/\\s]+)(?::(\\d*))?(?:\\/"
      "([^\\s?#]+)?([?][^?#]*)?(#.*)?)?$", (GRegexCompileFlags) 0, (GRegexMatchFlags) 0, NULL);
  ret = g_regex_match (regex, url, G_REGEX_MATCH_ANCHORED, NULL);
  g_regex_unref (regex);

  return ret;
}

static void
load_from_url (gchar * file_name, gchar * url)
{
  SoupSession *session;
  SoupMessage *msg;
  FILE *dst;

  session = soup_session_sync_new ();
  msg = soup_message_new ("GET", url);
  soup_session_send_message (session, msg);

  dst = fopen (file_name, "w+");

  if (dst == NULL) {
    GST_ERROR ("It is not possible to create the file");
    goto end;
  }
  fwrite (msg->response_body->data, 1, msg->response_body->length, dst);
  fclose (dst);

end:
  g_object_unref (msg);
  g_object_unref (session);
}

static void
kms_ear_detect_load_image_to_overlay (KmsEarDetect * eardetect)
{
  gchar *url = NULL;
  IplImage *costumeAux = NULL;
  gboolean fields_ok = TRUE;

  fields_ok = fields_ok
      && gst_structure_get (eardetect->priv->image_to_overlay,
      "offsetXPercent", G_TYPE_DOUBLE, &eardetect->priv->offsetXPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (eardetect->priv->image_to_overlay,
      "offsetYPercent", G_TYPE_DOUBLE, &eardetect->priv->offsetYPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (eardetect->priv->image_to_overlay,
      "widthPercent", G_TYPE_DOUBLE, &eardetect->priv->widthPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (eardetect->priv->image_to_overlay,
      "heightPercent", G_TYPE_DOUBLE, &eardetect->priv->heightPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (eardetect->priv->image_to_overlay, "url",
      G_TYPE_STRING, &url, NULL);

  if (!fields_ok) {
    GST_WARNING_OBJECT (eardetect, "Invalid image structure received");
    goto end;
  }

  if (url == NULL) {
    GST_DEBUG ("Unset the image overlay");
    goto end;
  }

  if (!eardetect->priv->dir_created) {
    gchar *d = g_strdup (TEMP_PATH);

    eardetect->priv->dir = g_mkdtemp (d);
    eardetect->priv->dir_created = TRUE;
  }

  costumeAux = cvLoadImage (url, CV_LOAD_IMAGE_UNCHANGED);

  if (costumeAux != NULL) {
    GST_DEBUG ("Image loaded from file");
    goto end;
  }

  if (is_valid_uri (url)) {
    gchar *file_name =
        g_strconcat (eardetect->priv->dir, "/image.png", NULL);
    load_from_url (file_name, url);
    costumeAux = cvLoadImage (file_name, CV_LOAD_IMAGE_UNCHANGED);
    g_remove (file_name);
    g_free (file_name);
  }

  if (costumeAux == NULL) {
    GST_ERROR_OBJECT (eardetect, "Overlay image not loaded");
  } else {
    GST_DEBUG_OBJECT (eardetect, "Image loaded from URL");
  }

end:

  if (eardetect->priv->costume != NULL) {
    cvReleaseImage (&eardetect->priv->costume);
    eardetect->priv->costume = NULL;
    eardetect->priv->view_ears = 0;
  }

  if (costumeAux != NULL) {
    eardetect->priv->costume = costumeAux;
    eardetect->priv->view_ears = 1;
  }

  g_free (url);
}

static void
kms_ear_detect_display_detections_overlay_img (KmsEarDetect * eardetect,
                                               int x, int y,
                                               int width, int height)
{
  IplImage *costumeAux;
  int w, h;
  uchar *row, *image_row;

  if ((eardetect->priv->heightPercent == 0) ||
      (eardetect->priv->widthPercent == 0)) {
    return;
  }

  x = x + (width * (eardetect->priv->offsetXPercent));
  y = y + (height * (eardetect->priv->offsetYPercent));
  height = height * (eardetect->priv->heightPercent);
  width = width * (eardetect->priv->widthPercent);

  costumeAux = cvCreateImage (cvSize (width, height),
      eardetect->priv->costume->depth,
      eardetect->priv->costume->nChannels);
  cvResize (eardetect->priv->costume, costumeAux, CV_INTER_LINEAR);

  row = (uchar *) costumeAux->imageData;
  image_row = (uchar *) eardetect->priv->img_orig->imageData +
      (y * eardetect->priv->img_orig->widthStep);

  for (h = 0; h < costumeAux->height; h++) {

    uchar *column = row;
    uchar *image_column = image_row + (x * 3);

    for (w = 0; w < costumeAux->width; w++) {
      /* Check if point is inside overlay boundaries */
      if (((w + x) < eardetect->priv->img_orig->width)
          && ((w + x) >= 0)) {
        if (((h + y) < eardetect->priv->img_orig->height)
            && ((h + y) >= 0)) {

          if (eardetect->priv->costume->nChannels == 1) {
            *(image_column) = (uchar) (*(column));
            *(image_column + 1) = (uchar) (*(column));
            *(image_column + 2) = (uchar) (*(column));
          } else if (eardetect->priv->costume->nChannels == 3) {
            *(image_column) = (uchar) (*(column));
            *(image_column + 1) = (uchar) (*(column + 1));
            *(image_column + 2) = (uchar) (*(column + 2));
          } else if (eardetect->priv->costume->nChannels == 4) {
            double proportion =
                ((double) *(uchar *) (column + 3)) / (double) 255;
            double overlay = SRC_OVERLAY * proportion;
            double original = 1 - overlay;

            *image_column =
                (uchar) ((*column * overlay) + (*image_column * original));
            *(image_column + 1) =
                (uchar) ((*(column + 1) * overlay) + (*(image_column +
                        1) * original));
            *(image_column + 2) =
                (uchar) ((*(column + 2) * overlay) + (*(image_column +
                        2) * original));
          }
        }
      }

      column += eardetect->priv->costume->nChannels;
      image_column += eardetect->priv->img_orig->nChannels;
    }

    row += costumeAux->widthStep;
    image_row += eardetect->priv->img_orig->widthStep;
  }

  cvReleaseImage (&costumeAux);
}

static void
kms_ear_detect_set_property (GObject *object, guint property_id,
			     const GValue *value, GParamSpec *pspec)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT (object);
  struct timeval  t; 
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex   
  KMS_EAR_DETECT_LOCK (ear_detect);

  switch (property_id) {
    
  case  PROP_VIEW_EARS:
    ear_detect->priv->view_ears = g_value_get_int (value);
    break;
       
  case PROP_DETECT_BY_EVENT:
    ear_detect->priv->detect_event =  g_value_get_int(value);
    break;

  case PROP_SEND_META_DATA:
    ear_detect->priv->meta_data =  g_value_get_int(value);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    ear_detect->priv->process_x_every_4_frames = g_value_get_int(value);    
    break;

  case PROP_MULTI_SCALE_FACTOR:
    ear_detect->priv->scale_factor = g_value_get_int(value);
    break;

  case PROP_WIDTH_TO_PROCESS:
    ear_detect->priv->width_to_process = g_value_get_int(value);
    break;


  case  PROP_ACTIVATE_SERVER_EVENTS:
    ear_detect->priv->server_events = g_value_get_int(value);
    gettimeofday(&t,NULL);
    ear_detect->priv->time_events_ms= ((t.tv_sec * 1000.0) + ((t.tv_usec)/1000.0));
    
    break;
    
  case PROP_SERVER_EVENTS_MS:
    ear_detect->priv->events_ms = g_value_get_int(value);
    break;   

  case PROP_IMAGE_TO_OVERLAY:
    if (ear_detect->priv->image_to_overlay != NULL)
      gst_structure_free (ear_detect->priv->image_to_overlay);

    ear_detect->priv->image_to_overlay = (GstStructure*) g_value_dup_boxed (value);
    kms_ear_detect_load_image_to_overlay (ear_detect);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);    
    break;
  }

  KMS_EAR_DETECT_UNLOCK (ear_detect);
}

static void
kms_ear_detect_get_property (GObject *object, guint property_id,
			     GValue *value, GParamSpec *pspec)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_EAR_DETECT_LOCK (ear_detect);

  switch (property_id) {

  case PROP_VIEW_EARS:
    g_value_set_int (value, ear_detect->priv->view_ears);
    break;

  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,ear_detect->priv->detect_event);
    break;
    
  case PROP_SEND_META_DATA:
    g_value_set_int(value,ear_detect->priv->meta_data);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    g_value_set_int(value,ear_detect->priv->process_x_every_4_frames);
    break;

  case PROP_MULTI_SCALE_FACTOR:
    g_value_set_int(value,ear_detect->priv->scale_factor);
    break;

  case PROP_WIDTH_TO_PROCESS:
    g_value_set_int(value,ear_detect->priv->width_to_process);
    break;

  case  PROP_ACTIVATE_SERVER_EVENTS:
    g_value_set_int(value,ear_detect->priv->server_events);
    break;
    
  case PROP_SERVER_EVENTS_MS:
    g_value_set_int(value,ear_detect->priv->events_ms);
    break;

  case PROP_IMAGE_TO_OVERLAY:
    if (ear_detect->priv->image_to_overlay == NULL) {
      ear_detect->priv->image_to_overlay =
          gst_structure_new_empty ("image_to_overlay");
    }
    g_value_set_boxed (value, ear_detect->priv->image_to_overlay);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_EAR_DETECT_UNLOCK (ear_detect);
}


static void kms_ear_detect_find_ears(KmsEarDetect *ear_detect, const Mat& face_img,
				     const Mat& ear_img,CascadeClassifier *ear_cascade,
				     double scale_f2e,double scale_e2o, 
				     double scale_f2o,int face_cols, int side)
{
  vector<Rect> *faces= ear_detect->priv->faces;
  vector<Rect> *ears;
  vector<Rect> ear;
  Mat earROI;

  
  //detecting faces
  fcascade.detectMultiScale( face_img, *faces,
			     MULTI_SCALE_FACTOR(ear_detect->priv->scale_factor), 2,
			     0 |CV_HAAR_SCALE_IMAGE,
			     Size(3, 3) );

  if ( 0 == faces->size() )
    return;

  if ( LEFT_SIDE == side )    
    ears= ear_detect->priv->lear;    
  else 
    ears= ear_detect->priv->rear;

  if (ears->size() > 0)
	ears->clear();
  else
    {
      if (ear_detect->priv->frames_with_no_detection < MAX_NUM_FPS_WITH_NO_DETECTION)
	ear_detect->priv->frames_with_no_detection+=1;
      else 
	{
	  ear_detect->priv->frames_with_no_detection =0;
	  ears->clear(); 	    	    
	}
    }
      
  for( vector<Rect>::iterator r = faces->begin(); r != faces->end(); r++)
    {
      const int top_height=cvRound((float)r->height*TOP_PERCENTAGE/100);
      const int down_height=cvRound((float)r->height*DOWN_PERCENTAGE/100);

      //printing faces
      //Transforming the point detected in face image to ear coordinates
      //we only take the down half of the face to avoid excessive processing	
      if (LEFT_SIDE == side)
	{	 
	  r->y=(r->y+top_height)*scale_f2e;
	  r->x=(r->x + (r->width/2))*scale_f2e;
	  r->height = (r->height-down_height)*scale_f2e;
	  r->width = (r->width/2 )*scale_f2e+EXTRA_ROI;
	  ////EXTRA_ROI As the ear is in the extreme of the face, 
	  //we need an extra area to detect it in right way	  
	  if (r->x + r->width > ear_img.cols) r->width=ear_img.cols - r->x -1; 
	}
      else 
	{
	  r->y=(r->y + top_height)*scale_f2e;
	  r->x=(face_cols- r->x - r->width)*scale_f2e-EXTRA_ROI;
	  r->height = (r->height-down_height)*scale_f2e;
	  r->width = (r->width/2)*scale_f2e;        
	  //EXTRA_ROI: As the ear is in the extreme of the face, 
	  //we need an extra area to detect it in right way
	  if (r->x<0 ) r->x=0;
	}
            
      earROI = ear_img(*r);
      ear_cascade->detectMultiScale( earROI, ear,
				     EAR_SCALE_FACTOR, 3, 
				     0| CV_HAAR_FIND_BIGGEST_OBJECT,
				     Size(1, 1));
            
      for ( vector<Rect>::iterator e = ear.begin(); e != ear.end();e++)
	{
	  //Transforming the point detected in ear image to oring coordinates	  
	  Rect e_aux;
	  e_aux.x= cvRound((r->x + e->x)*scale_e2o);
	  e_aux.y= cvRound((r->y + e->y)*scale_e2o);
	  e_aux.width=(e->width-1)*scale_e2o;
	  e_aux.height=(e->height-1)*scale_e2o;
	  ears->push_back(e_aux);
	}
    }
	  
}

static void kms_ear_detect_print_ears(KmsEarDetect *ear_detect, int side)
{  
  vector<Rect> *ears;
  Scalar color;
  int j=0;
  const static Scalar colors[] =  { CV_RGB(0,0,255),
				    CV_RGB(0,128,255),
				    CV_RGB(0,255,255),
				    CV_RGB(0,255,0),
				    CV_RGB(255,128,0),
				    CV_RGB(255,255,0),
				    CV_RGB(255,0,0),
				    CV_RGB(255,0,255)};	

  if ( LEFT_SIDE == side )    
    ears= ear_detect->priv->lear;    
  else 
    ears= ear_detect->priv->rear;
  
  for ( vector<Rect>::iterator e = ears->begin(); e != ears->end();e++ , j++)
    {	
      if (ear_detect->priv->costume == NULL) {
        color = colors[j%8];
        cvRectangle( ear_detect->priv->img_orig,
         cvPoint(e->x,e->y),
         cvPoint(cvRound(e->x + e->width),
           cvRound(e->y + e->height-1)),
         color, 3, 8, 0);
      } else {
        kms_ear_detect_display_detections_overlay_img (ear_detect, e->x, e->y, e->width, e->height);
      }
    }

}


static void kms_ear_detect_process_frame(KmsEarDetect *ear_detect,int width,int height,
					 double scale_f2e,double scale_e2o, double scale_f2o)
{  
    
  Mat img (ear_detect->priv->img_orig);
  Mat ear_frame (cvRound(img.rows/scale_e2o), cvRound(img.cols/scale_e2o), CV_8UC1);
  Mat gray;
  Mat  leftImg( cvRound (img.rows/scale_f2o), cvRound(img.cols/scale_f2o), CV_8UC1 );
  Mat  rightImg( cvRound (img.rows/scale_f2o), cvRound(img.cols/scale_f2o), CV_8UC1 );

  ear_detect->priv->num_frame++;
  
  if ( (2 == ear_detect->priv->process_x_every_4_frames && // one every 2 images
	(1 == ear_detect->priv->num_frame % 2)) ||  
       ( (2 != ear_detect->priv->process_x_every_4_frames) &&
	 (ear_detect->priv->num_frame <= ear_detect->priv->process_x_every_4_frames)))    
    {
      ear_detect->priv->num_frames_to_process--;  
      
      cvtColor( img, gray, CV_BGR2GRAY );
      //setting up the image where the face detector will be executed
      resize( gray, leftImg, leftImg.size(), 0, 0, INTER_LINEAR );
      equalizeHist( leftImg, leftImg );
    
      //setting up the image where the ear detector will be executed
      resize(gray,ear_frame,ear_frame.size(), 0,0,INTER_LINEAR);
      equalizeHist( ear_frame, ear_frame);
      

      kms_ear_detect_find_ears(ear_detect,leftImg,ear_frame,&lecascade,
			       scale_f2e,scale_e2o,scale_f2o,
			       leftImg.cols,LEFT_SIDE);

      flip(leftImg,rightImg,1);//flip around y-axis
      kms_ear_detect_find_ears(ear_detect,rightImg,ear_frame,&recascade,
			       scale_f2e,scale_e2o,scale_f2o,
			       leftImg.cols,RIGHT_SIDE);

    }     

  
  if (GOP == ear_detect->priv->num_frame )
    ear_detect->priv->num_frame=0;


  if (1 == ear_detect->priv->view_ears)
    {    

      kms_ear_detect_print_ears(ear_detect,RIGHT_SIDE);
      kms_ear_detect_print_ears(ear_detect,LEFT_SIDE);

    }
  
  

}


 

/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_ear_detect_transform_frame_ip (GstVideoFilter *filter,
				   GstVideoFrame *frame)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT (filter);
  GstMapInfo info;
  double scale_f2o=0.0,scale_e2o=0.0,scale_f2e=0.0;
  int width=0,height=0;

  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
  // setting up images
  kms_ear_detect_conf_images (ear_detect, frame, info);

  KMS_EAR_DETECT_LOCK (ear_detect);

  scale_f2e = ear_detect->priv->scale_f2e;
  scale_e2o= ear_detect->priv->scale_e2o;
  scale_f2o = ear_detect->priv->scale_f2o;
  width = ear_detect->priv->img_width;
  height = ear_detect->priv->img_height;

  KMS_EAR_DETECT_UNLOCK (ear_detect);

  kms_ear_detect_process_frame(ear_detect,width,height,scale_f2e,scale_e2o,scale_f2o);        


  kms_ear_send_event(ear_detect,frame);
    

  ear_detect->priv->faces->clear();

  gst_buffer_unmap (frame->buffer, &info);

  
  return GST_FLOW_OK;
}

/*
 * In dispose(), you are supposed to free all types referenced from this
 * object which might themselves hold a reference to self. Generally,
 * the most simple solution is to unref all members on which you own a
 * reference.
 * dispose() might be called multiple times, so we must guard against
 * calling g_object_unref() on an invalid GObject by setting the member
 * NULL; g_clear_object() does this for us, atomically.
 */
static void
kms_ear_detect_dispose (GObject *object)
{
}

static int
delete_file (const char *fpath, const struct stat *sb, int typeflag,
    struct FTW *ftwbuf)
{
  int rv = g_remove (fpath);

  if (rv) {
    GST_WARNING ("Error deleting file: %s. %s", fpath, strerror (errno));
  }

  return rv;
}

static void
remove_recursive (const gchar * path)
{
  nftw (path, delete_file, 64, FTW_DEPTH | FTW_PHYS);
}

/*
 * The finalize function is called when the object is destroyed.
 */
static void
kms_ear_detect_finalize (GObject *object)
{
  KmsEarDetect *ear_detect = KMS_EAR_DETECT(object);

  cvReleaseImage (&ear_detect->priv->img_orig);

  if (ear_detect->priv->costume != NULL)
    cvReleaseImage (&ear_detect->priv->costume);

  if (ear_detect->priv->image_to_overlay != NULL)
    gst_structure_free (ear_detect->priv->image_to_overlay);

  if (ear_detect->priv->dir_created) {
    remove_recursive (ear_detect->priv->dir);
    g_free (ear_detect->priv->dir);
  }

  delete ear_detect->priv->faces;
  delete ear_detect->priv->ears;
  g_rec_mutex_clear(&ear_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_ear_detect_init (KmsEarDetect *
		     ear_detect)
{
  int ret=0;
  ear_detect->priv = KMS_EAR_DETECT_GET_PRIVATE (ear_detect);
  ear_detect->priv->scale_f2e=1.0;
  ear_detect->priv->scale_e2o=1.0;
  ear_detect->priv->scale_f2o=1.0;
  ear_detect->priv->img_width = 320;
  ear_detect->priv->img_height = 240;
  ear_detect->priv->img_orig = NULL;
  ear_detect->priv->view_ears=-1;
  ear_detect->priv->show_faces=1;
  ear_detect->priv->discard=1;
  ear_detect->priv->faces= new vector<Rect>;
  ear_detect->priv->ears= new vector<Rect>;
  ear_detect->priv->rear= new vector<Rect>;
  ear_detect->priv->lear= new vector<Rect>;

  ear_detect->priv->num_frames_to_process=0;
  ear_detect->priv->process_x_every_4_frames=PROCESS_ALL_FRAMES;
  ear_detect->priv->num_frame=0;
  ear_detect->priv->scale_factor=DEFAULT_SCALE_FACTOR;
  ear_detect->priv->width_to_process=EAR_WIDTH;
  ear_detect->priv->frames_with_no_detection=0;

  if (fcascade.empty())
    if (kms_ear_detect_init_cascade() < 0)      
      GST_DEBUG("Error: init cascade");
  
  if (ret != 0)
    GST_DEBUG ("Error reading the haar cascade configuration file");
  
  g_rec_mutex_init(&ear_detect->priv->mutex);
  
}

static void
kms_ear_detect_class_init (KmsEarDetectClass *ear)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (ear);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (ear);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (ear),
				      gst_pad_template_new ("src", GST_PAD_SRC,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (ear),
				      gst_pad_template_new ("sink", GST_PAD_SINK,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (ear),
					 "ear detection filter element", "Video/Filter",
					 "Fade detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_ear_detect_set_property;
  gobject_class->get_property = kms_ear_detect_get_property;
  gobject_class->dispose = kms_ear_detect_dispose;
  gobject_class->finalize = kms_ear_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_EARS,
				   g_param_spec_int ("view-ears", "view ears",
						     "To indicate whether or not we have to draw  the detected ears on the stream",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE) );


  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event. This algorithm does not support any specific event at the moment. Therefore this property should not be used",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the ear and profile face", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_WIDTH_TO_PROCESS,
				   g_param_spec_int ("width-to-process", "width to process",
						     "160,320 (default),480,640 => this will be the width of the image that the algorithm is going to process to detect ears", 
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

  g_object_class_install_property (gobject_class, PROP_IMAGE_TO_OVERLAY,
      g_param_spec_boxed ("image-to-overlay", "image to overlay",
          "set the url of the image to overlay the faces",
          GST_TYPE_STRUCTURE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_ear_detect_transform_frame_ip);

  kms_ear_detector_signals[SIGNAL_ON_EAR_EVENT] =
    g_signal_new ("ear-event",
		  G_TYPE_FROM_CLASS (ear),
		  G_SIGNAL_RUN_LAST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 1, G_TYPE_STRING);

  /*Properties initialization*/
  g_type_class_add_private (ear, sizeof (KmsEarDetectPrivate) );
}

gboolean
kms_ear_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_EAR_DETECT);
}
