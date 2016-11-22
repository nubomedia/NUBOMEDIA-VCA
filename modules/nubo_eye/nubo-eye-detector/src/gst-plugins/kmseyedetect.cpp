#include "kmseyedetect.h"
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
#include <libsoup/soup.h>
#include <ftw.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

#define PLUGIN_NAME "nuboeyedetector"
#define FACE_WIDTH 160
#define EYE_WIDTH 320
#define DEFAULT_FILTER_TYPE (KmsEyeDetectType)0
#define FACE_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_alt.xml"
#define EYE_LEFT_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_lefteye.xml"
#define EYE_RIGHT_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_mcs_righteye.xml"

#define TOP_PERCENTAGE 25
#define DOWN_PERCENTAGE 40
#define SIDE_PERCENTAGE 0
#define NUM_FRAMES_TO_PROCESS 10
#define FACE_TYPE "face"
#define SERVER_EVENTS 0
#define EVENTS_MS 30001

#define PROCESS_ALL_FRAMES 4
#define GOP 4
#define DEFAULT_SCALE_FACTOR 25
#define EYE_SCALE_FACTOR 1.1
#define DEFAULT_EUCLIDEAN_DIS 7
#define MAX_NUM_FPS_WITH_NO_DETECTION 1

#define TEMP_PATH "/tmp/XXXXXX"
#define SRC_OVERLAY ((double)1)

using namespace cv;

#define KMS_EYE_DETECT_LOCK(eye_detect)					\
  (g_rec_mutex_lock (&( (KmsEyeDetect *) eye_detect)->priv->mutex))

#define KMS_EYE_DETECT_UNLOCK(eye_detect)				\
  (g_rec_mutex_unlock (&( (KmsEyeDetect *) eye_detect)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_eye_detect_debug_category);
#define GST_CAT_DEFAULT kms_eye_detect_debug_category

#define KMS_EYE_DETECT_GET_PRIVATE(obj) (				\
					 G_TYPE_INSTANCE_GET_PRIVATE (	\
								      (obj), \
								      KMS_TYPE_EYE_DETECT, \
								      KmsEyeDetectPrivate \
									) \
									)

enum {
  PROP_0,
  PROP_VIEW_EYES,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA,  
  PROP_MULTI_SCALE_FACTOR,
  PROP_WIDTH_TO_PROCESS,
  PROP_ACTIVATE_SERVER_EVENTS,
  PROP_SERVER_EVENTS_MS,
  PROP_PROCESS_X_EVERY_4_FRAMES,
  PROP_IMAGE_TO_OVERLAY
};


enum {
  SIGNAL_ON_EYE_EVENT,
  LAST_SIGNAL
};

static guint kms_eye_detector_signals[LAST_SIGNAL] = { 0 };

struct _KmsEyeDetectPrivate {

  IplImage *img_orig;  
  float scale_o2f;//origin 2 face
  float scale_o2e;//orig  2 eye
  float scale_f2e;//face  2 eye

  int img_width;
  int img_height;
  int view_eyes;
  GRecMutex mutex;
  gboolean debug;
  GQueue *events_queue;
  GstClockTime pts;
  int num_frames_to_process;
  int detect_event;
  int meta_data;
  int width_to_process; 
  int process_x_every_4_frames;
  int scale_factor;
  int num_frame;
  int server_events;
  int events_ms;
  double time_events_ms;
  vector<Rect> *faces;
  vector<Rect> *eyes_l;
  vector<Rect> *eyes_r;
  int frames_with_no_detection_el;
  int frames_with_no_detection_er;
  /*detect event*/
  // 0(default) => will always run the alg; 
  // 1=> will only run the alg if the filter receive some special event
  /*meta_data*/
  //0 (default) => it will not send meta data;
  //1 => it will send the bounding box of the eye as metadata 
  /*num_frames_to_process*/
  // When we receive an event we need to process at least NUM_FRAMES_TO_PROCESS
  GstStructure *image_to_overlay;
  gdouble offsetXPercent, offsetYPercent, widthPercent, heightPercent;
  IplImage *costume;
  gboolean dir_created;
  gchar *dir;

  std::shared_ptr <CascadeClassifier> fcascade;
  std::shared_ptr <CascadeClassifier> eyes_rcascade;
  std::shared_ptr <CascadeClassifier> eyes_lcascade;
};

/* pad templates */

#define VIDEO_SRC_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

#define VIDEO_SINK_CAPS				\
  GST_VIDEO_CAPS_MAKE("{ BGR }")

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsEyeDetect, kms_eye_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_eye_detect_debug_category,
						  PLUGIN_NAME, 0,			  
						  "debug category for sample_filter element") );

#define MULTI_SCALE_FACTOR(scale) (1 + scale*1.0/100)

template<typename T>
string toString(const T& value)
{
  std::stringstream ss;
   ss << value;
   return ss.str();
}


static int
kms_eye_detect_init_cascade(KmsEyeDetect *eye_detect)
{
  eye_detect->priv->fcascade = std::make_shared<CascadeClassifier>();
  eye_detect->priv->eyes_lcascade = std::make_shared<CascadeClassifier>();
  eye_detect->priv->eyes_rcascade = std::make_shared<CascadeClassifier>();

  if (!eye_detect->priv->fcascade->load(FACE_CONF_FILE) )
    {
      std::cerr << "ERROR: Could not load face cascade" << std::endl;
      return -1;
    }
  
  if (!eye_detect->priv->eyes_rcascade->load(EYE_RIGHT_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load eye right cascade" << std::endl;
      return -1;
    }
  
  if (!eye_detect->priv->eyes_lcascade->load(EYE_LEFT_CONF_FILE))
    {
      std::cerr << "ERROR: Could not load eye left cascade" << std::endl;
      return -1;
    }
  
  return 0;
}

static gboolean kms_eye_detect_sink_events(GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;
  KmsEyeDetect *eye = KMS_EYE_DETECT(trans);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *message;

      GST_OBJECT_LOCK (eye);

      message = gst_structure_copy (gst_event_get_structure (event));

      g_queue_push_tail (eye->priv->events_queue, message);

      GST_OBJECT_UNLOCK (eye);
      break;
    }
  default:
    break;
  }

  ret=  gst_pad_event_default (trans->sinkpad, GST_OBJECT (trans), event);

  return ret;
}

static void kms_eye_send_event(KmsEyeDetect *eye_detect,GstVideoFrame *frame)
{
  GstStructure *face,*eye;
  GstStructure *ts;
  GstStructure *message;
  int i=0;
  char elem_id[10];
  vector<Rect> *fd=eye_detect->priv->faces;
  vector<Rect> *ed_l=eye_detect->priv->eyes_l;
  vector<Rect> *ed_r=eye_detect->priv->eyes_r;
  int norm_faces = eye_detect->priv->scale_o2f;
  std::string eyes_str = "";
  struct timeval  end; 
  double current_t, diff_time;
  GstEvent *event;

  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);
	
  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
		

  /*eyes are already normalized*/
  for(  vector<Rect>::const_iterator m = ed_l->begin(); m != ed_l->end(); m++,i++ )
    {
      eye = gst_structure_new("eye_left",
			      "type", G_TYPE_STRING,"eye", 
			      "x", G_TYPE_UINT,(guint) m->x, 
			      "y", G_TYPE_UINT,(guint) m->y, 
			      "width",G_TYPE_UINT, (guint)m->width ,
			      "height",G_TYPE_UINT, (guint)m->height ,
			      NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, eye,NULL);
      gst_structure_free(eye);
      
         //neccesary info for sending as event to the server
      std::string new_eye ("x:" + toString((guint) m->x ) + 
			    ",y:" + toString((guint) m->y ) + 
			    ",width:" + toString((guint)m->width )+ 
			    ",height:" + toString((guint)m->height )+ ";");
      eyes_str= eyes_str + new_eye;
    }

  for(  vector<Rect>::const_iterator m = ed_r->begin(); m != ed_r->end(); m++,i++ )
    {
      eye = gst_structure_new("eye_right",
			      "type", G_TYPE_STRING,"eye", 
			      "x", G_TYPE_UINT,(guint) m->x, 
			      "y", G_TYPE_UINT,(guint) m->y, 
			      "width",G_TYPE_UINT, (guint)m->width,
			      "height",G_TYPE_UINT, (guint)m->height,
			      NULL);
      sprintf(elem_id,"%d",i);
      gst_structure_set(message,elem_id,GST_TYPE_STRUCTURE, eye,NULL);
      gst_structure_free(eye);
      
      std::string new_eye ("x:" + toString((guint) m->x ) + 
			    ",y:" + toString((guint) m->y ) + 
			    ",width:" + toString((guint)m->width )+ 
			    ",height:" + toString((guint)m->height )+ ";");
      eyes_str= eyes_str + new_eye;
    }

  
  if ((int)ed_r->size()>0 || (int)ed_l->size()>0)
    {
      
      gettimeofday(&end,NULL);
      current_t= ((end.tv_sec * 1000.0) + ((end.tv_usec)/1000.0));
      diff_time = current_t - eye_detect->priv->time_events_ms;
            
      if (1 == eye_detect->priv->server_events && diff_time > eye_detect->priv->events_ms)
	{
	  eye_detect->priv->time_events_ms=current_t;
	  g_signal_emit (G_OBJECT (eye_detect),
	  		 kms_eye_detector_signals[SIGNAL_ON_EYE_EVENT], 0,eyes_str.c_str());
	}
            /*Adding data as a metadata in the video*/
      /* if (1 == eye_detect->priv->meta_data)    	
	 kms_buffer_add_serializable_meta (frame->buffer,message); */
    }

  //post a faces detected event to src pad7
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(eye_detect->base.element.srcpad, event); 
}

static void
kms_eye_detect_conf_images (KmsEyeDetect *eye_detect,
			    GstVideoFrame *frame, GstMapInfo &info)
{
  
  eye_detect->priv->img_height = frame->info.height;
  eye_detect->priv->img_width  = frame->info.width;
  
  if ( ((eye_detect->priv->img_orig != NULL)) &&
       ((eye_detect->priv->img_orig->width != frame->info.width)
	|| (eye_detect->priv->img_orig->height != frame->info.height)) )
    {
      cvReleaseImage(&eye_detect->priv->img_orig);
      eye_detect->priv->img_orig = NULL;
    }
  
  if (eye_detect->priv->img_orig == NULL)
    eye_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							   frame->info.height),
						    IPL_DEPTH_8U, 3);
  
  if (eye_detect->priv->detect_event) 
    /*If we receive faces through event, the coordinates are normalized to the img orig size*/
    eye_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)frame->info.width);
  else 
    eye_detect->priv->scale_o2f = ((float)frame->info.width) / ((float)FACE_WIDTH);
  
  eye_detect->priv->scale_o2e = ((float)frame->info.width) / ((float)eye_detect->priv->width_to_process);
  
  eye_detect->priv->scale_f2e = ((float)eye_detect->priv->scale_o2f) / ((float)eye_detect->priv->scale_o2e);
  eye_detect->priv->img_orig->imageData = (char *) info.data;
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
kms_eye_detect_load_image_to_overlay (KmsEyeDetect * eyedetect)
{
  gchar *url = NULL;
  IplImage *costumeAux = NULL;
  gboolean fields_ok = TRUE;

  fields_ok = fields_ok
      && gst_structure_get (eyedetect->priv->image_to_overlay,
      "offsetXPercent", G_TYPE_DOUBLE, &eyedetect->priv->offsetXPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (eyedetect->priv->image_to_overlay,
      "offsetYPercent", G_TYPE_DOUBLE, &eyedetect->priv->offsetYPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (eyedetect->priv->image_to_overlay,
      "widthPercent", G_TYPE_DOUBLE, &eyedetect->priv->widthPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (eyedetect->priv->image_to_overlay,
      "heightPercent", G_TYPE_DOUBLE, &eyedetect->priv->heightPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (eyedetect->priv->image_to_overlay, "url",
      G_TYPE_STRING, &url, NULL);

  if (!fields_ok) {
    GST_WARNING_OBJECT (eyedetect, "Invalid image structure received");
    goto end;
  }

  if (url == NULL) {
    GST_DEBUG ("Unset the image overlay");
    goto end;
  }

  if (!eyedetect->priv->dir_created) {
    gchar *d = g_strdup (TEMP_PATH);

    eyedetect->priv->dir = g_mkdtemp (d);
    eyedetect->priv->dir_created = TRUE;
  }

  costumeAux = cvLoadImage (url, CV_LOAD_IMAGE_UNCHANGED);

  if (costumeAux != NULL) {
    GST_DEBUG ("Image loaded from file");
    goto end;
  }

  if (is_valid_uri (url)) {
    gchar *file_name =
        g_strconcat (eyedetect->priv->dir, "/image.png", NULL);
    load_from_url (file_name, url);
    costumeAux = cvLoadImage (file_name, CV_LOAD_IMAGE_UNCHANGED);
    g_remove (file_name);
    g_free (file_name);
  }

  if (costumeAux == NULL) {
    GST_ERROR_OBJECT (eyedetect, "Overlay image not loaded");
  } else {
    GST_DEBUG_OBJECT (eyedetect, "Image loaded from URL");
  }

end:

  if (eyedetect->priv->costume != NULL) {
    cvReleaseImage (&eyedetect->priv->costume);
    eyedetect->priv->costume = NULL;
    eyedetect->priv->view_eyes = 0;
  }

  if (costumeAux != NULL) {
    eyedetect->priv->costume = costumeAux;
    eyedetect->priv->view_eyes = 1;
  }

  g_free (url);
}

static void
kms_eye_detect_display_detections_overlay_img (KmsEyeDetect * eyedetect,
                                                int x, int y,
                                                int width, int height)
{
  IplImage *costumeAux;
  int w, h;
  uchar *row, *image_row;

  if ((eyedetect->priv->heightPercent == 0) ||
      (eyedetect->priv->widthPercent == 0)) {
    return;
  }

  x = x + (width * (eyedetect->priv->offsetXPercent));
  y = y + (height * (eyedetect->priv->offsetYPercent));
  height = height * (eyedetect->priv->heightPercent);
  width = width * (eyedetect->priv->widthPercent);

  costumeAux = cvCreateImage (cvSize (width, height),
      eyedetect->priv->costume->depth,
      eyedetect->priv->costume->nChannels);
  cvResize (eyedetect->priv->costume, costumeAux, CV_INTER_LINEAR);

  row = (uchar *) costumeAux->imageData;
  image_row = (uchar *) eyedetect->priv->img_orig->imageData +
      (y * eyedetect->priv->img_orig->widthStep);

  for (h = 0; h < costumeAux->height; h++) {

    uchar *column = row;
    uchar *image_column = image_row + (x * 3);

    for (w = 0; w < costumeAux->width; w++) {
      /* Check if point is inside overlay boundaries */
      if (((w + x) < eyedetect->priv->img_orig->width)
          && ((w + x) >= 0)) {
        if (((h + y) < eyedetect->priv->img_orig->height)
            && ((h + y) >= 0)) {

          if (eyedetect->priv->costume->nChannels == 1) {
            *(image_column) = (uchar) (*(column));
            *(image_column + 1) = (uchar) (*(column));
            *(image_column + 2) = (uchar) (*(column));
          } else if (eyedetect->priv->costume->nChannels == 3) {
            *(image_column) = (uchar) (*(column));
            *(image_column + 1) = (uchar) (*(column + 1));
            *(image_column + 2) = (uchar) (*(column + 2));
          } else if (eyedetect->priv->costume->nChannels == 4) {
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

      column += eyedetect->priv->costume->nChannels;
      image_column += eyedetect->priv->img_orig->nChannels;
    }

    row += costumeAux->widthStep;
    image_row += eyedetect->priv->img_orig->widthStep;
  }

  cvReleaseImage (&costumeAux);
}

static void
kms_eye_detect_set_property (GObject *object, guint property_id,
			     const GValue *value, GParamSpec *pspec)
{
  KmsEyeDetect *eye_detect = KMS_EYE_DETECT (object);
  struct timeval  t;
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_EYE_DETECT_LOCK (eye_detect);

  switch (property_id) {

  case PROP_VIEW_EYES:
    eye_detect->priv->view_eyes = g_value_get_int (value);
    break;
  case PROP_DETECT_BY_EVENT:
    eye_detect->priv->detect_event =  g_value_get_int(value);
    break;
    
  case PROP_SEND_META_DATA:
    eye_detect->priv->meta_data =  g_value_get_int(value);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    eye_detect->priv->process_x_every_4_frames = g_value_get_int(value);    
    break;

  case PROP_MULTI_SCALE_FACTOR:
    eye_detect->priv->scale_factor = g_value_get_int(value);
    break;

  case PROP_WIDTH_TO_PROCESS:
    eye_detect->priv->width_to_process = g_value_get_int(value);
    break;

    case  PROP_ACTIVATE_SERVER_EVENTS:
    eye_detect->priv->server_events = g_value_get_int(value);
    gettimeofday(&t,NULL);
    eye_detect->priv->time_events_ms= ((t.tv_sec * 1000.0) + ((t.tv_usec)/1000.0));    
    break;
    
  case PROP_SERVER_EVENTS_MS:
    eye_detect->priv->events_ms = g_value_get_int(value);
    break;   

  case PROP_IMAGE_TO_OVERLAY:
    if (eye_detect->priv->image_to_overlay != NULL)
      gst_structure_free (eye_detect->priv->image_to_overlay);

    eye_detect->priv->image_to_overlay = (GstStructure*) g_value_dup_boxed (value);
    kms_eye_detect_load_image_to_overlay (eye_detect);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_EYE_DETECT_UNLOCK (eye_detect);
}

static void
kms_eye_detect_get_property (GObject *object, guint property_id,
			     GValue *value, GParamSpec *pspec)
{
  KmsEyeDetect *eye_detect = KMS_EYE_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_EYE_DETECT_LOCK (eye_detect);

  switch (property_id) {

  case PROP_VIEW_EYES:
    g_value_set_int (value, eye_detect->priv->view_eyes);
    break;
    
  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,eye_detect->priv->detect_event);
    break;
    
  case PROP_SEND_META_DATA:
    g_value_set_int(value,eye_detect->priv->meta_data);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    g_value_set_int(value,eye_detect->priv->process_x_every_4_frames);
    break;

  case PROP_MULTI_SCALE_FACTOR:
    g_value_set_int(value,eye_detect->priv->scale_factor);
    break;

  case PROP_WIDTH_TO_PROCESS:
    g_value_set_int(value,eye_detect->priv->width_to_process);
    break;

  case  PROP_ACTIVATE_SERVER_EVENTS:
    g_value_set_int(value,eye_detect->priv->server_events);
    break;
    
  case PROP_SERVER_EVENTS_MS:
    g_value_set_int(value,eye_detect->priv->events_ms);
    break;

  case PROP_IMAGE_TO_OVERLAY:
    if (eye_detect->priv->image_to_overlay == NULL) {
      eye_detect->priv->image_to_overlay =
          gst_structure_new_empty ("image_to_overlay");
    }
    g_value_set_boxed (value, eye_detect->priv->image_to_overlay);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_EYE_DETECT_UNLOCK (eye_detect);
}

static gboolean __get_timestamp(KmsEyeDetect *eye,
				GstStructure *message)
{
  GstStructure *ts;
  gboolean ret=false;

  ret = gst_structure_get(message,"timestamp",GST_TYPE_STRUCTURE, &ts,NULL);

  if (ret) {
    
    gst_structure_get(ts,"pts",G_TYPE_UINT64,
		      &eye->priv->pts,NULL);
    gst_structure_free(ts);
  }

  return ret;
}

static bool __get_event_message(KmsEyeDetect *eye,
				GstStructure *message)
{
  gint len,aux;
  bool result=false;
  int  id=0;
  char struct_id[10];
  const gchar *str_type;

  len = gst_structure_n_fields (message);

  eye->priv->faces->clear();

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
      
      if (g_strcmp0(str_type,FACE_TYPE)==0)//only interested on FACE EVENTS
	{
	  
	  Rect r;
	  gst_structure_get (data, "x", G_TYPE_UINT, & r.x, NULL);
	  gst_structure_get (data, "y", G_TYPE_UINT, & r.y, NULL);
	  gst_structure_get (data, "width", G_TYPE_UINT, & r.width, NULL);
	  gst_structure_get (data, "height", G_TYPE_UINT, & r.height, NULL);	  	 
	  gst_structure_free (data);
	  eye->priv->faces->push_back(r);
	}	  
      result=true;
    }
  }
              
  return result;
}

static bool __receive_event(KmsEyeDetect *eye_detect, GstVideoFrame *frame)
{
  KmsSerializableMeta *metadata;
  gboolean res = false;
  GstStructure *message;
  gboolean ret = false;
  //if detect_event is false it does not matter the event received

  if (0==eye_detect->priv->detect_event) return true;

  if (g_queue_get_length(eye_detect->priv->events_queue) == 0) 
    return false;
  
  message= (GstStructure *) g_queue_pop_head(eye_detect->priv->events_queue);
  
  if (NULL != message)
    {
      
      ret=__get_timestamp(eye_detect,message);
      
      if ( ret )
	{
	  res = __get_event_message(eye_detect,message);	  
	}
    }

  /*metadata = kms_buffer_get_serializable_meta(frame->buffer);

  if (NULL == metadata)   
    return false;

    res = __get_event_message(eye_detect,metadata->data);	  */

  if (res) 
    eye_detect->priv->num_frames_to_process = NUM_FRAMES_TO_PROCESS / 
      (5 - eye_detect->priv->process_x_every_4_frames); ;
  
  return res;
}

static bool __contain_bb(Point p,Rect r)
{ 
  if ((p.y >= r.y && p.y  <= r.y + r.height) &&
      (p.x >= r.x && p.x  <= r.x + r.width) )
    {

      return true;
    }
    
  return false;
}

static void __merge_eyes_current_frame( Rect face_bb, std::vector<Rect> *eye_r, std::vector<Rect> &eyes, int scale, bool eye_left )
{
int i =0;
  vector<Rect>::iterator it;
  Point eye_center,eye_center_2;
  /*As the number of eyes is higher than one, we need to get only one*/
  
  for (i=eyes.size()-1;i>0 ; i--)
    {/*We compare the different eyes. To delete one of them, this has to be included in the 
      burble of other eye and has to have a bigger area*/

      eye_center.x =   eyes[i].x + eyes[i].width/2;
      eye_center.y =   eyes[i].y + eyes[i].height/2;
      if (__contain_bb(eye_center,eyes[i-1]) &&  eyes[i].area() < eyes[i-1].area())	
	eyes.erase(eyes.end()-i-1);      
      else 
	{
	  eye_center.x =   eyes[i-1].x + eyes[i-1].width/2;
	  eye_center.y =   eyes[i-1].y + eyes[i-1].height/2;	  
	  
	  if (__contain_bb(eye_center,eyes[i]) && eyes[i-1].area() < eyes[i].area())	    
	    eyes.erase(eyes.end()-i);	     
	}
    }
 
  /*We need to modify the y axis for  all the eyes which have been located at the top of ROI. 
    Since, the eyebrow can lead errors*/

  i = eyes.size();  
  for( vector<Rect>::reverse_iterator r = eyes.rbegin(); r != eyes.rend(); ++r )
    {
      i--;
      int y_aux= face_bb.y*scale + face_bb.height*scale*60/100;
      
      if ((face_bb.y*scale + eyes[i].y < y_aux) )	
	{	  
	  if ( i == 0 && eyes.size() == 1 )
	    {
	      if (eye_r->size()>0 && eye_left )		
		eyes[i].y = eye_r->at(0).y;
	      
	    }
	  else 	    
	    eyes.erase(--r.base());	      
	}	
    }
  /*the size of the vector can not be > 1, because we are seeking for an eye in a 
    small part of the image (approximately between the ear and nose), if this happens
    we need  to find the eye_center closest to the left and center middle.*/
    
  if (eyes.size()>1)
    {
      int middle_y = face_bb.x*scale + face_bb.height*scale/2;
      int middle_x = face_bb.y*scale + face_bb.width*scale/2;
      
      for (i=eyes.size()-1;i>0;i--)
	{
	  eye_center.y   =   eyes[i].y + eyes[i].height/2;
	  eye_center.x   =   eyes[i].x + eyes[i].width/2;
	  eye_center_2.y =   eyes[i-1].y + eyes[i-1].height/2;
	  eye_center_2.x =   eyes[i-1].x + eyes[i-1].width/2;
	    
	  float sqrt_1point= sqrt(pow(middle_x - eye_center.x,2) + 
				  pow(middle_y - eye_center.y,2));
	  float sqrt_2point = sqrt(pow(middle_x - eye_center_2.x,2) + 
				   pow(middle_y - eye_center_2.y,2) );
	  	    
	  if (sqrt_1point < sqrt_2point)
	    eyes.erase(eyes.end()-i-1);
	  else	      
	    eyes.erase(eyes.end()-i);  	  
	}	      	
    }    

  /*The left eye presents some problems with eyebrow. As a consequence the y coordinate
   is located on the eyebrow, to fix that we put exactly the same Y coordinate than in the 
  right eye*/  
  if (eye_left)  
    {
      if ( (eye_r->size() > 0) && (eyes.size()> 0) && eye_left)
	  eyes[0].y=eye_r->at(0).y;

    }

}

static vector<Rect> *__merge_eyes_consecutives_frames(vector<Rect> *ce, vector<Rect> *eyes,
						      Rect &face_cord, int scale, bool eye_left)
{
  vector<Rect>::iterator it_e ;
  vector<Rect> *res = new vector<Rect>;


  for (int i=0; i < (int)(eyes->size()); i++)
    {
      Point old_center;
      old_center.x = eyes->at(i).x + eyes->at(i).width/2;
      old_center.y = eyes->at(i).y + eyes->at(i).height/2;

      for (int j=0; j < (int)(ce->size()); j++)
	{
	  Point new_center;	  
	  new_center.x = ce->at(j).x + (ce->at(j).width )/2;
	  new_center.y = ce->at(j).y + (ce->at(j).height)/2;
	  double h2 = sqrt(pow((new_center.x -old_center.x),2) + 
			   pow((new_center.y - old_center.y),2));	  
	  if (h2 < DEFAULT_EUCLIDEAN_DIS)
	    { /*As the difference among pixels is very low, we mantain the coordinates of the
		previous eye in order to avoid vibrations*/	      
	      res->push_back(eyes->at(i));
	      ce->erase(ce->begin()+j);	      
	      break;
	    }
	}     
    } 


  if (ce->size() > 0)
      for(it_e = ce->begin(); it_e != ce->end();it_e++)
	res->push_back(*it_e);

  return res;
}

static void transform_2_global_coordinates(vector<Rect> *eye_v,Rect face_cord,int scale)
{
  vector<Rect>::iterator it_e ;

  for(it_e = eye_v->begin(); it_e != eye_v->end();it_e++)
    {
      it_e->x= (face_cord.x + it_e->x)*scale;
      it_e->y= (face_cord.y + it_e->y)*scale;
      it_e->width=  (it_e->width-1)*scale;
      it_e->height=  (it_e->height-1)*scale;      
    }
}

static void
kms_eye_detect_process_frame(KmsEyeDetect *eye_detect,int width,int height,double scale_o2f,
			     double scale_o2e,double scale_f2e, GstVideoFrame *frame)
{
  Mat img (eye_detect->priv->img_orig);
  Mat f_faces(cvRound(img.rows/scale_o2f),cvRound(img.cols/scale_o2f),CV_8UC1);
  Mat frame_gray;
  Mat eye_frame (cvRound(img.rows/scale_o2e), cvRound(img.cols/scale_o2e), CV_8UC1);
  Rect f_aux_r,f_aux_l;
  int down_height=0;
  int top_height=0;
  int k=0;
  std::vector<Rect> *faces=eye_detect->priv->faces;
  std::vector<Rect> *eyes_r=eye_detect->priv->eyes_r;
  std::vector<Rect> *eyes_l=eye_detect->priv->eyes_l;
  vector<Rect> eye_r,eye_l;
  vector<Rect> *res_r = new vector<Rect>;
  vector<Rect> *res_l = new vector<Rect>;
  Rect r_aux;
  Rect eye_right;

  if ( ! __receive_event(eye_detect,frame) && eye_detect->priv->num_frames_to_process <=0)
    return;

  eye_detect->priv->num_frame++;

  
  if ( (2 == eye_detect->priv->process_x_every_4_frames && // one every 2 images
	(1 == eye_detect->priv->num_frame % 2)) ||  
       ( (2 != eye_detect->priv->process_x_every_4_frames) &&
	 (eye_detect->priv->num_frame <= eye_detect->priv->process_x_every_4_frames)))    
    {

      eye_detect->priv->num_frames_to_process--;    
      cvtColor( img, frame_gray, COLOR_BGR2GRAY );
      equalizeHist( frame_gray, frame_gray );

      /*To detect the faces we need to work in 320 240*/
      //if detect_event != 0 we have received faces as meta-data
      if (0 == eye_detect->priv->detect_event )
	{      
	  resize(frame_gray,f_faces,f_faces.size(),0,0,INTER_LINEAR);
	  faces->clear();
    eye_detect->priv->fcascade->detectMultiScale(f_faces, *faces,
				    MULTI_SCALE_FACTOR(eye_detect->priv->scale_factor),
				    3, 0, Size(30, 30));   //1.2
	}

      resize(frame_gray,eye_frame,eye_frame.size(), 0,0,INTER_LINEAR);
      equalizeHist( eye_frame, eye_frame);

      int i = 0;

      for( vector<Rect>::iterator r = faces->begin(); r != faces->end(); r++, i++ )
	{   
    vector<Rect> *result_aux = NULL;
	  /*To detect eyes we need to work in the normal width 640 480*/
	  r_aux.x = r->x*scale_f2e;
	  r_aux.y = r->y*scale_f2e;
	  r_aux.width = r->width*scale_f2e;
	  r_aux.height  = r->height*scale_f2e;
	  
	  /*Clearing the area of the forehead and chin, 
	    and taking only the left side of the face*/
	  down_height=cvRound((float)r_aux.height*DOWN_PERCENTAGE/100);
	  top_height=cvRound((float)r_aux.height*TOP_PERCENTAGE/100);
	  
	  /****** RIGHT EYE ******/
	  f_aux_r.x= r_aux.x;
	  f_aux_r.y= r_aux.y + top_height;
	  f_aux_r.height= r_aux.height - top_height - down_height;
	  f_aux_r.width = r_aux.width/2;
	  
	  Mat faceROI = eye_frame(f_aux_r);            
	  //-- In each face, detect eyes. The pointed obtained are related to the ROI
	  if (eye_r.size() > 0 ) eye_r.clear();
    eye_detect->priv->eyes_rcascade->detectMultiScale( faceROI, eye_r,EYE_SCALE_FACTOR , 2,
					  0 |CASCADE_SCALE_IMAGE, 
					  Size(20, 20));	  	    

	  /****** LEFT EYE ******/
	  f_aux_l.x=  r_aux.x + r_aux.width/2;	
	  f_aux_l.y=  r_aux.y + top_height;
	  f_aux_l.height= r_aux.height - top_height- down_height;
	  f_aux_l.width = r_aux.width/2;	 
	  
	  faceROI = eye_frame(f_aux_l); 
	  
    eye_detect->priv->eyes_lcascade->detectMultiScale( faceROI, eye_l, EYE_SCALE_FACTOR,  2,
					  0 |CASCADE_SCALE_IMAGE, 
					  Size(20, 20) );

	  /****** WORKING WiTH GLOBAL COORDINATES ******/
	  transform_2_global_coordinates(&eye_r,f_aux_r,scale_o2e);
	  transform_2_global_coordinates(&eye_l,f_aux_l,scale_o2e);

	  /****** SELECTING THE BEST VALUE FOR EVERY EYE ******/
	  if (eye_r.size() > 0)
	    {
	      __merge_eyes_current_frame(f_aux_r,&eye_r,eye_r,scale_o2e, false);
	      result_aux= __merge_eyes_consecutives_frames(&eye_r,eyes_r,f_aux_r,scale_o2e,false);
	      for ( k=0; k < (int)(result_aux->size()); k++)				
		res_r->push_back(result_aux->at(k));				
	    }

	  if (eye_l.size() > 0)
	    {
        if (result_aux != NULL) {
          if (result_aux->size()>0) result_aux->clear();
        }
	      
	      __merge_eyes_current_frame(f_aux_l,res_r,eye_l,scale_o2e,true);	      
	      result_aux= __merge_eyes_consecutives_frames(&eye_l,eyes_l,f_aux_l,scale_o2e,true);  

	      for ( k=0; k < (int)(result_aux->size()); k++)		
		res_l->push_back(result_aux->at(k));
	    }	  
	}
      
      if (res_r->size() < 1)
	//Not results found for right eye
	if (eye_detect->priv->frames_with_no_detection_er < MAX_NUM_FPS_WITH_NO_DETECTION)
	  eye_detect->priv->frames_with_no_detection_er +=1;
	else {
	  eye_detect->priv->frames_with_no_detection_er =0;
	  eye_detect->priv->eyes_r->clear();
	}
      else //result found
	{
	  eye_detect->priv->frames_with_no_detection_er =0;
	  eye_detect->priv->eyes_r->clear();
	  for (k=0;k < (int)(res_r->size());k++)
	    eye_detect->priv->eyes_r->push_back(res_r->at(k));
	}

      if (res_l->size() < 1)
	//Not results found for right eye
	if (eye_detect->priv->frames_with_no_detection_el < MAX_NUM_FPS_WITH_NO_DETECTION)
	  eye_detect->priv->frames_with_no_detection_el +=1;
	else {
	  eye_detect->priv->frames_with_no_detection_el =0;
	  eye_detect->priv->eyes_l->clear();
	}
      else //result found
	{
	  eye_detect->priv->frames_with_no_detection_el =0;
	  eye_detect->priv->eyes_l->clear();
	  for (k=0;k < (int)(res_l->size());k++)
	    eye_detect->priv->eyes_l->push_back(res_l->at(k));
	}
    }       
  
  if (GOP == eye_detect->priv->num_frame )
    eye_detect->priv->num_frame=0;
    
  /*Here we only have one BB per eye_x*/
  if (1 == eye_detect->priv->view_eyes )
    {
      int radius=-1;
      
      if (eyes_r->size() > 0)
	{	  
	  Point eye_center_r( (*eyes_r)[0].x + (*eyes_r)[0].width/2,
			      (*eyes_r)[0].y + (*eyes_r)[0].height/2 );
	  radius = cvRound( ((*eyes_r)[0].width + (*eyes_r)[0].height)*0.25 );
    if (eye_detect->priv->costume == NULL) {
      circle( img, eye_center_r, radius, Scalar( 255, 0, 0 ), 4, 8, 0 );
    } else {
      kms_eye_detect_display_detections_overlay_img (eye_detect, (*eyes_r)[0].x,
          (*eyes_r)[0].y, (*eyes_r)[0].width, (*eyes_r)[0].height );
    }
	}
      
      if (eyes_l->size() > 0)
	{
	  Point eye_center_l(  (*eyes_l)[0].x + (*eyes_l)[0].width/2,
			       (*eyes_l)[0].y + (*eyes_l)[0].height/2 );
	  if (radius < 0)
	    radius = cvRound( ((*eyes_l)[0].width + (*eyes_l)[0].height)*0.25 );
    if (eye_detect->priv->costume == NULL) {
      circle( img, eye_center_l, radius, Scalar( 255, 0, 0 ), 4, 8, 0 );
    } else {
      kms_eye_detect_display_detections_overlay_img (eye_detect, (*eyes_l)[0].x,
          (*eyes_l)[0].y, (*eyes_l)[0].width, (*eyes_l)[0].height );
    }
    }
  }
}

/** 
 * This function contains the image processing.
 */
static GstFlowReturn
kms_eye_detect_transform_frame_ip (GstVideoFilter *filter,
				   GstVideoFrame *frame)
{
  KmsEyeDetect *eye_detect = KMS_EYE_DETECT (filter);
  GstMapInfo info;
  double scale_o2f=0.0,scale_o2e=0.0,scale_f2e;
  int width=0,height=0;
    
  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);
  //setting up images
  kms_eye_detect_conf_images (eye_detect, frame, info);

  KMS_EYE_DETECT_LOCK (eye_detect);

  scale_o2f = eye_detect->priv->scale_o2f;
  scale_o2e= eye_detect->priv->scale_o2e;
  scale_f2e = eye_detect->priv->scale_f2e;

  width = eye_detect->priv->img_width;
  height = eye_detect->priv->img_height;
	
  KMS_EYE_DETECT_UNLOCK (eye_detect);

  kms_eye_detect_process_frame(eye_detect,width,height,scale_o2f,
			       scale_o2e,scale_f2e,frame);
  

  kms_eye_send_event(eye_detect,frame);

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
 * NULL; g_cleye_object() does this for us, atomically.
 */
static void
kms_eye_detect_dispose (GObject *object)
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
kms_eye_detect_finalize (GObject *object)
{
  KmsEyeDetect *eye_detect = KMS_EYE_DETECT(object);

  cvReleaseImage (&eye_detect->priv->img_orig);

  if (eye_detect->priv->costume != NULL)
    cvReleaseImage (&eye_detect->priv->costume);

  if (eye_detect->priv->image_to_overlay != NULL)
    gst_structure_free (eye_detect->priv->image_to_overlay);

  if (eye_detect->priv->dir_created) {
    remove_recursive (eye_detect->priv->dir);
    g_free (eye_detect->priv->dir);
  }

  delete eye_detect->priv->faces;
  delete eye_detect->priv->eyes_l;
  delete eye_detect->priv->eyes_r;
  g_rec_mutex_clear(&eye_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_eye_detect_init (KmsEyeDetect *
		     eye_detect)
{
  int ret=0;
  eye_detect->priv = KMS_EYE_DETECT_GET_PRIVATE (eye_detect);
  eye_detect->priv->img_width = 320;
  eye_detect->priv->img_height = 240;
  eye_detect->priv->img_orig = NULL;
  eye_detect->priv->scale_o2f=1.0;
  eye_detect->priv->scale_o2e=1.0;
  eye_detect->priv->view_eyes=0;
  eye_detect->priv->events_queue= g_queue_new();
  eye_detect->priv->detect_event=0;
  eye_detect->priv->meta_data=0;
  eye_detect->priv->faces= new vector<Rect>;
  eye_detect->priv->eyes_l= new vector<Rect>;
  eye_detect->priv->eyes_r= new vector<Rect>;
  eye_detect->priv->num_frames_to_process=0;
  eye_detect->priv->frames_with_no_detection_el=0;
  eye_detect->priv->frames_with_no_detection_er=0;

  eye_detect->priv->process_x_every_4_frames=PROCESS_ALL_FRAMES;
  eye_detect->priv->num_frame=0;
  eye_detect->priv->scale_factor=DEFAULT_SCALE_FACTOR;
  eye_detect->priv->width_to_process=EYE_WIDTH;
  eye_detect->priv->server_events =SERVER_EVENTS;
  eye_detect->priv->events_ms =EVENTS_MS;


  kms_eye_detect_init_cascade(eye_detect);
  if (eye_detect->priv->fcascade == NULL)
    GST_DEBUG ("Error reading the haar cascade configuration file");

  g_rec_mutex_init(&eye_detect->priv->mutex);

}

static void
kms_eye_detect_class_init (KmsEyeDetectClass *eye)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (eye);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (eye);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (eye),
				      gst_pad_template_new ("src", GST_PAD_SRC,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (eye),
				      gst_pad_template_new ("sink", GST_PAD_SINK,
							    GST_PAD_ALWAYS,
							    gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (eye),
					 "eye detection filter element", "Video/Filter",
					 "Eye detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_eye_detect_set_property;
  gobject_class->get_property = kms_eye_detect_get_property;
  gobject_class->dispose = kms_eye_detect_dispose;
  gobject_class->finalize = kms_eye_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_EYES,
				   g_param_spec_int ("view-eyes", "view eyes",
						     "To indicate whether or not we have to draw  the detected eye on the stream",
						     0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  
  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event like face detection",
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("send-meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the eye and face", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_WIDTH_TO_PROCESS,
				   g_param_spec_int ("width-to-process", "width to process",
						     "160,320 (default),480,640 => this will be the width of the image that the algorithm is going to process to detect eyes", 
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
          "set the url of the image to overlay the eyes",
          GST_TYPE_STRUCTURE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (kms_eye_detect_transform_frame_ip);

  kms_eye_detector_signals[SIGNAL_ON_EYE_EVENT] =
    g_signal_new ("eye-event",
		  G_TYPE_FROM_CLASS (eye),
		  G_SIGNAL_RUN_LAST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 1, G_TYPE_STRING);

  /*Properties initialization*/
  g_type_class_add_private (eye, sizeof (KmsEyeDetectPrivate) );

  eye->base_eye_detect_class.parent_class.sink_event =
    GST_DEBUG_FUNCPTR(kms_eye_detect_sink_events);
}

gboolean
kms_eye_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_EYE_DETECT);
}
