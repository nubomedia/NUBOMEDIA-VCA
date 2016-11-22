#include "kmsfacedetect.h"
#include "Faces.hpp"
//#include "FaceInfo.hpp"

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

#include <commons/kms-core-marshal.h>
#include <libsoup/soup.h>
#include <ftw.h>
#include <memory>

#define PLUGIN_NAME "nubofacedetector"
#define DEFAULT_FILTER_TYPE (KmsFaceDetectType)0
#define NUM_FRAMES_TO_PROCESS 10
#define PROCESS_ALL_FRAMES 4
#define DEFAULT_SCALE_FACTOR 25
#define DEFAULT_WIDTH 160
#define DEFAULT_HEIGHT 240
#define GOP 4
#define MOTION_EVENT "motion"
#define MAX_NUM_FPS_WITH_NO_DETECTION 1
#define DEFAULT_EUCLIDEAN_DIS 8
#define TRACK_MAXIMUM_DISTANCE 40
#define AREA_THRESHOLD 500
#define SERVER_EVENTS 0
#define EVENTS_MS 30001

#define TEMP_PATH "/tmp/XXXXXX"
#define SRC_OVERLAY ((double)1)

#define HAAR_CONF_FILE "/usr/share/opencv/haarcascades/haarcascade_frontalface_alt.xml"

using namespace cv;

#define KMS_FACE_DETECT_LOCK(face_detect)				\
  (g_rec_mutex_lock (&( (KmsFaceDetect *) face_detect)->priv->mutex))

#define KMS_FACE_DETECT_UNLOCK(face_detect)				\
  (g_rec_mutex_unlock (&( (KmsFaceDetect *) face_detect)->priv->mutex))


GST_DEBUG_CATEGORY_STATIC (kms_face_detect_debug_category);
#define GST_CAT_DEFAULT kms_face_detect_debug_category

#define KMS_FACE_DETECT_GET_PRIVATE(obj) (				\
					  G_TYPE_INSTANCE_GET_PRIVATE (	\
								       (obj), \
								       KMS_TYPE_FACE_DETECT, \
								       KmsFaceDetectPrivate \
									) \
									)
enum {
  PROP_0,
  PROP_VIEW_FACES,
  PROP_DETECT_BY_EVENT,
  PROP_SEND_META_DATA,
  PROP_MULTI_SCALE_FACTOR,
  PROP_WIDTH_TO_PROCCESS,
  PROP_PROCESS_X_EVERY_4_FRAMES,
  PROP_EUCLIDEAN_THRESHOLD,
  PROP_TRACK_THRESHOLD,
  PROP_AREA_THRESHOLD,
  PROP_ACTIVATE_SERVER_EVENTS,
  PROP_SERVER_EVENTS_MS,
  PROP_SHOW_DEBUG_INFO,
  PROP_IMAGE_TO_OVERLAY
};



enum {
  SIGNAL_ON_FACE_EVENT,
  LAST_SIGNAL
};

static guint kms_face_detector_signals[LAST_SIGNAL] = { 0 };

struct _KmsFaceDetectPrivate {

  IplImage *img_orig;
  IplImage *img_resized;
  CvMemStorage *cv_mem_storage;
  CvSeq *face_seq;
  int img_width;
  int img_height;
  int width_to_process; 
  float scale;
  int show_faces; // to draw a rectangle over the face
  int detect_event;  
  int meta_data;
  int num_frames_to_process;
  int scale_factor;
  int process_x_every_4_frames;
  int num_frame;
  int euclidean_threshold;
  int track_threshold;
  int area_threshold;
  int server_events;
  int events_ms;
  double time_events_ms;
  std::shared_ptr <CascadeClassifier> cascade;
  GstClockTime dts,pts;
  GQueue *events_queue;
  GRecMutex mutex;
  gboolean debug;
  int num_iter;
  int frames_with_no_detection;
  //Faces faces_detected; 
  //vector<Rect> *faces_detected;
  Faces *faces_detected;

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
G_DEFINE_TYPE_WITH_CODE (KmsFaceDetect, kms_face_detect,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (kms_face_detect_debug_category,
						  PLUGIN_NAME, 0,
						  "debug category for sample_filter element") );

#define MULTI_SCALE_FACTOR(scale) (1 + scale*1.0/100)

const Scalar BaseFace::colors[]={ CV_RGB(0,0,255),
				  CV_RGB(0,128,255),
				  CV_RGB(0,255,255),
				  CV_RGB(0,255,0),
				  CV_RGB(255,128,0),
				  CV_RGB(255,255,0),
				  CV_RGB(255,0,0),
				  CV_RGB(255,0,255)};


template<typename T>
string toString(const T& value)
{
   stringstream ss;
   ss << value;
   return ss.str();
}

static int
kms_face_detect_init_cascade(KmsFaceDetect *face_detect)
{
  face_detect->priv->cascade = std::make_shared<CascadeClassifier>();

  if (!face_detect->priv->cascade->load(HAAR_CONF_FILE))
  {
    GST_ERROR ("Error charging cascade");
    return -1;
  }

  if (face_detect->priv->cascade->empty())
    GST_ERROR ("****CASCADE IS EMPTY***********");

  return 0;
}

static void kms_face_send_event(KmsFaceDetect *face_detect,GstVideoFrame *frame, int width2process)
{
  GstStructure *face;
  GstStructure *ts;
  GstEvent *event;
  GstStructure *message;
  int i=0;
  char face_id[10];
  Faces *faces = face_detect->priv->faces_detected;    
  vector<Rect> *fd=  new vector<Rect>;
  //vector<BaseFace> *bf = new vector<BaseFace>;
  int norm_scale = face_detect->priv->img_width / width2process;
  std::string faces_str;
  struct timeval  end; 
  double current_t, diff_time;

  faces->get_faces(fd);
  message= gst_structure_new_empty("message");
  ts=gst_structure_new("time",
		       "pts",G_TYPE_UINT64, GST_BUFFER_PTS(frame->buffer),NULL);

  gst_structure_set(message,"timestamp",GST_TYPE_STRUCTURE, ts,NULL);
  gst_structure_free(ts);
  
  for( vector<Rect>::const_iterator r = fd->begin(); r != fd->end(); r++,i++ )
    {
      //neccesary info for sending as metadata
      face = gst_structure_new("face",
			       "type", G_TYPE_STRING,"face", 
			       "x", G_TYPE_UINT,(guint) r->x * norm_scale, 
			       "y", G_TYPE_UINT,(guint) r->y * norm_scale, 
			       "width",G_TYPE_UINT, (guint)r->width * norm_scale ,
			       "height",G_TYPE_UINT, (guint)r->height * norm_scale,
			       NULL);
      sprintf(face_id,"%d",i);
      gst_structure_set(message,face_id,GST_TYPE_STRUCTURE, face,NULL);
      gst_structure_free(face);

      //neccesary info for sending as event to the server
      std::string new_face ("x:" + toString((guint) r->x *norm_scale) + 
			    ",y:" + toString((guint) r->y *norm_scale) + 
			    ",width:" + toString((guint)r->width * norm_scale)+ 
			    ",height:" + toString((guint)r->height * norm_scale)+ ";");
      faces_str= faces_str + new_face;
    }

  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, message);
  gst_pad_push_event(face_detect->base.element.srcpad, event);

  if ((int)fd->size()>0)
    {

      
      gettimeofday(&end,NULL);
      current_t= ((end.tv_sec * 1000.0) + ((end.tv_usec)/1000.0));
      diff_time = current_t - face_detect->priv->time_events_ms;

      if (1 == face_detect->priv->server_events && diff_time > face_detect->priv->events_ms)
	{
	  face_detect->priv->time_events_ms=current_t;
	  g_signal_emit (G_OBJECT (face_detect),
			 kms_face_detector_signals[SIGNAL_ON_FACE_EVENT], 0,faces_str.c_str());
	}
      
      /*Adding data as a metadata in the video*/
      /*if (1 == face_detect->priv->meta_data)    	
	kms_buffer_add_serializable_meta (frame->buffer,message);  */	
    }


}

//delete this function when metadata will be used
static gboolean kms_face_detect_sink_events(GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;
  KmsFaceDetect *face = KMS_FACE_DETECT(trans);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstStructure *message;

      GST_OBJECT_LOCK (face);

      message = gst_structure_copy (gst_event_get_structure (event));
      g_queue_push_tail (face->priv->events_queue, message);

      GST_OBJECT_UNLOCK (face);
      break;
    }
  default:
    KMS_FACE_DETECT_LOCK (face);
    GST_BASE_TRANSFORM_CLASS (kms_face_detect_parent_class)->sink_event (trans, event);
    KMS_FACE_DETECT_UNLOCK (face);
    break;
  }
  //ret = gst_pad_push_event (trans->srcpad, event);
  //ret=  gst_pad_event_default (trans->sinkpad, GST_OBJECT (trans), event);

  return ret;
}

static void
kms_face_detect_conf_images (KmsFaceDetect *face_detect,
                             GstVideoFrame *frame, GstMapInfo &info)
{

  face_detect->priv->img_height = frame->info.height;
  face_detect->priv->img_width  = frame->info.width; 

  if ( ((face_detect->priv->img_orig != NULL)) &&
       ((face_detect->priv->img_orig->width != frame->info.width)
	|| (face_detect->priv->img_orig->height != frame->info.height)) )
    {
      cvReleaseImage(&face_detect->priv->img_orig);
      face_detect->priv->img_orig = NULL;
    }

  if (face_detect->priv->img_orig == NULL)

    face_detect->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
							    frame->info.height),
						     IPL_DEPTH_8U, 3);

  face_detect->priv->scale = frame->info.width / face_detect->priv->width_to_process;
  face_detect->priv->img_orig->imageData = (char *) info.data;
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
kms_face_detect_load_image_to_overlay (KmsFaceDetect * facedetect)
{
  gchar *url = NULL;
  IplImage *costumeAux = NULL;
  gboolean fields_ok = TRUE;

  fields_ok = fields_ok
      && gst_structure_get (facedetect->priv->image_to_overlay,
      "offsetXPercent", G_TYPE_DOUBLE, &facedetect->priv->offsetXPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (facedetect->priv->image_to_overlay,
      "offsetYPercent", G_TYPE_DOUBLE, &facedetect->priv->offsetYPercent,
      NULL);
  fields_ok = fields_ok
      && gst_structure_get (facedetect->priv->image_to_overlay,
      "widthPercent", G_TYPE_DOUBLE, &facedetect->priv->widthPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (facedetect->priv->image_to_overlay,
      "heightPercent", G_TYPE_DOUBLE, &facedetect->priv->heightPercent, NULL);
  fields_ok = fields_ok
      && gst_structure_get (facedetect->priv->image_to_overlay, "url",
      G_TYPE_STRING, &url, NULL);

  if (!fields_ok) {
    GST_WARNING_OBJECT (facedetect, "Invalid image structure received");
    goto end;
  }

  if (url == NULL) {
    GST_DEBUG ("Unset the image overlay");
    goto end;
  }

  if (!facedetect->priv->dir_created) {
    gchar *d = g_strdup (TEMP_PATH);

    facedetect->priv->dir = g_mkdtemp (d);
    facedetect->priv->dir_created = TRUE;
  }

  costumeAux = cvLoadImage (url, CV_LOAD_IMAGE_UNCHANGED);

  if (costumeAux != NULL) {
    GST_DEBUG ("Image loaded from file");
    goto end;
  }

  if (is_valid_uri (url)) {
    gchar *file_name =
        g_strconcat (facedetect->priv->dir, "/image.png", NULL);
    load_from_url (file_name, url);
    costumeAux = cvLoadImage (file_name, CV_LOAD_IMAGE_UNCHANGED);
    g_remove (file_name);
    g_free (file_name);
  }

  if (costumeAux == NULL) {
    GST_ERROR_OBJECT (facedetect, "Overlay image not loaded");
  } else {
    GST_DEBUG_OBJECT (facedetect, "Image loaded from URL");
  }

end:

  if (facedetect->priv->costume != NULL) {
    cvReleaseImage (&facedetect->priv->costume);
    facedetect->priv->costume = NULL;
    facedetect->priv->show_faces = 0;
  }

  if (costumeAux != NULL) {
    facedetect->priv->costume = costumeAux;
    facedetect->priv->show_faces = 1;
  }

  g_free (url);
}

static void
kms_face_detect_display_detections_overlay_img (KmsFaceDetect * facedetect,
                                                int x, int y,
                                                int width, int height)
{
  IplImage *costumeAux;
  int w, h;
  uchar *row, *image_row;

  if ((facedetect->priv->heightPercent == 0) ||
      (facedetect->priv->widthPercent == 0)) {
    return;
  }

  x = x + (width * (facedetect->priv->offsetXPercent));
  y = y + (height * (facedetect->priv->offsetYPercent));
  height = height * (facedetect->priv->heightPercent);
  width = width * (facedetect->priv->widthPercent);

  costumeAux = cvCreateImage (cvSize (width, height),
      facedetect->priv->costume->depth,
      facedetect->priv->costume->nChannels);
  cvResize (facedetect->priv->costume, costumeAux, CV_INTER_LINEAR);

  row = (uchar *) costumeAux->imageData;
  image_row = (uchar *) facedetect->priv->img_orig->imageData +
      (y * facedetect->priv->img_orig->widthStep);

  for (h = 0; h < costumeAux->height; h++) {

    uchar *column = row;
    uchar *image_column = image_row + (x * 3);

    for (w = 0; w < costumeAux->width; w++) {
      /* Check if point is inside overlay boundaries */
      if (((w + x) < facedetect->priv->img_orig->width)
          && ((w + x) >= 0)) {
        if (((h + y) < facedetect->priv->img_orig->height)
            && ((h + y) >= 0)) {

          if (facedetect->priv->costume->nChannels == 1) {
            *(image_column) = (uchar) (*(column));
            *(image_column + 1) = (uchar) (*(column));
            *(image_column + 2) = (uchar) (*(column));
          } else if (facedetect->priv->costume->nChannels == 3) {
            *(image_column) = (uchar) (*(column));
            *(image_column + 1) = (uchar) (*(column + 1));
            *(image_column + 2) = (uchar) (*(column + 2));
          } else if (facedetect->priv->costume->nChannels == 4) {
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

      column += facedetect->priv->costume->nChannels;
      image_column += facedetect->priv->img_orig->nChannels;
    }

    row += costumeAux->widthStep;
    image_row += facedetect->priv->img_orig->widthStep;
  }

  cvReleaseImage (&costumeAux);
}

static void
kms_face_detect_set_property (GObject *object, guint property_id,
			      const GValue *value, GParamSpec *pspec)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT (object);
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  struct timeval  t;
  char buffer[256];
  memset (buffer,0,256);

  KMS_FACE_DETECT_LOCK (face_detect);

  switch (property_id) {

  case PROP_VIEW_FACES:
    face_detect->priv->show_faces =  g_value_get_int (value);
    break;

  case PROP_DETECT_BY_EVENT:
    face_detect->priv->detect_event =  g_value_get_int(value);
    break;

  case PROP_SEND_META_DATA:
    face_detect->priv->meta_data =  g_value_get_int(value);
    break;

  case PROP_PROCESS_X_EVERY_4_FRAMES:
    face_detect->priv->process_x_every_4_frames = g_value_get_int(value);
    break;

  case PROP_WIDTH_TO_PROCCESS:
    face_detect->priv->width_to_process = g_value_get_int(value);
    break;

  case PROP_MULTI_SCALE_FACTOR:
    face_detect->priv->scale_factor = g_value_get_int(value);
    break;

  case PROP_EUCLIDEAN_THRESHOLD:
    face_detect->priv->euclidean_threshold = g_value_get_int(value);
    break;

  case PROP_TRACK_THRESHOLD:
    face_detect->priv->euclidean_threshold = g_value_get_int(value);
    break;

  case PROP_AREA_THRESHOLD:
    face_detect->priv->area_threshold = g_value_get_int(value);
    break;
    
  case  PROP_ACTIVATE_SERVER_EVENTS:
    face_detect->priv->server_events = g_value_get_int(value);
    gettimeofday(&t,NULL);
    face_detect->priv->time_events_ms= ((t.tv_sec * 1000.0) + ((t.tv_usec)/1000.0));
    
    break;
    
  case PROP_SERVER_EVENTS_MS:
    face_detect->priv->events_ms = g_value_get_int(value);
    break;

  case PROP_IMAGE_TO_OVERLAY:
    if (face_detect->priv->image_to_overlay != NULL)
      gst_structure_free (face_detect->priv->image_to_overlay);

    face_detect->priv->image_to_overlay = (GstStructure*) g_value_dup_boxed (value);
    kms_face_detect_load_image_to_overlay (face_detect);
    break;
     
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_FACE_DETECT_UNLOCK (face_detect);

}

static void
kms_face_detect_get_property (GObject *object, guint property_id,

			      GValue *value, GParamSpec *pspec)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT (object);

  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  KMS_FACE_DETECT_LOCK (face_detect);

  switch (property_id) {

  case PROP_VIEW_FACES:
    g_value_set_int (value, face_detect->priv->show_faces);
    break;

  case PROP_DETECT_BY_EVENT:
    g_value_set_int(value,face_detect->priv->detect_event);
    break;

  case PROP_SEND_META_DATA:
    g_value_set_int(value,face_detect->priv->meta_data);
    break;
    
  case PROP_PROCESS_X_EVERY_4_FRAMES:
    g_value_set_int(value,face_detect->priv->process_x_every_4_frames);
    break;

  case PROP_MULTI_SCALE_FACTOR:
    g_value_set_int(value,face_detect->priv->scale_factor);
    break;

  case PROP_WIDTH_TO_PROCCESS:
    g_value_set_int(value,face_detect->priv->width_to_process);
    break;

  case PROP_EUCLIDEAN_THRESHOLD:
    g_value_set_int(value,face_detect->priv->euclidean_threshold);
    break;

  case PROP_TRACK_THRESHOLD:
    g_value_set_int(value,face_detect->priv->track_threshold);
    break;

  case PROP_AREA_THRESHOLD:
    g_value_set_int(value,face_detect->priv->area_threshold);
    break;

  case  PROP_ACTIVATE_SERVER_EVENTS:
    g_value_set_int(value,face_detect->priv->server_events);
    break;
    
  case PROP_SERVER_EVENTS_MS:
    g_value_set_int(value,face_detect->priv->events_ms);
    break;

  case PROP_IMAGE_TO_OVERLAY:
    if (face_detect->priv->image_to_overlay == NULL) {
      face_detect->priv->image_to_overlay =
          gst_structure_new_empty ("image_to_overlay");
    }
    g_value_set_boxed (value, face_detect->priv->image_to_overlay);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  KMS_FACE_DETECT_UNLOCK (face_detect);
}

static gboolean __get_timestamp(KmsFaceDetect *face,
				GstStructure *message)
{
  GstStructure *ts;
  gboolean ret=false;

  ret = gst_structure_get(message,"timestamp",GST_TYPE_STRUCTURE, &ts,NULL);

  if (ret) {

    gst_structure_get(ts,"dts",G_TYPE_UINT64,
		      &face->priv->dts,NULL);
    gst_structure_get(ts,"pts",G_TYPE_UINT64,
		      &face->priv->pts,NULL);
    gst_structure_free(ts);
  }

  return ret;
}



static bool __get_event_message(GstStructure *message)
{
  gint len,aux;
  bool result=false;
  gchar *grid=0;
  len = gst_structure_n_fields (message);


  for (aux = 0; aux < len; aux++) {
    GstStructure *data;
    gboolean ret;

    const gchar *name = gst_structure_nth_field_name (message, aux);
    if (g_strcmp0 (name, "timestamp") == 0) {
      continue;
    }


    if (0 == g_strcmp0 (name, MOTION_EVENT)) 
      {
	ret = gst_structure_get (message, name, GST_TYPE_STRUCTURE, &data, NULL);

	if (ret) {
	  gst_structure_get (data, "grid", G_TYPE_STRING, &grid, NULL);
	  gst_structure_free (data);
	  result=true;
	}
      }
  }

   return result;
}

static bool __receive_event(KmsFaceDetect *face_detect, GstVideoFrame *frame)
{
  bool res=false;
  GstStructure *message;
  KmsSerializableMeta *metadata;
  gboolean ret=false;

  //Uncomment the following line to use the metadata
  //metadata=kms_buffer_get_serializable_meta(frame->buffer);

  //if detect_event is false it does not matter the event received
  if (0==face_detect->priv->detect_event) {

    return true;
  }

  if (g_queue_get_length(face_detect->priv->events_queue) == 0) 
    return false;
  //Uncomment the following line to use the metadata
  /*if ( NULL == metadata) 
    return false;*/

  message= (GstStructure *) g_queue_pop_head(face_detect->priv->events_queue);
  if (NULL != message)
    {
      ret=__get_timestamp(face_detect,message);
      
      if ( ret )
	{
	  res = __get_event_message(message);	  
	}
      
    }

  //Uncomment the following line to use the metadata
  /*res = __get_event_message(metadata->data);	  */
     
  if (res) 
    face_detect->priv->num_frames_to_process = NUM_FRAMES_TO_PROCESS;
  

  return res;
}

static void
kms_face_detect_process_frame(KmsFaceDetect *face_detect,int width,int height,double scale,
			      GstVideoFrame *frame)
{
  Mat img (face_detect->priv->img_orig);
  Scalar color;
  Mat aux_img;
  Mat img_gray;

  try {
    int rows_size = img.rows;
    int cols_size = img.cols;

    if (cvRound (img.rows/scale) > 0) {
      rows_size = cvRound (img.rows/scale);
    } else {
      scale =1;
    }

    if (cvRound (img.cols/scale) > 0) {
      cols_size = cvRound (img.cols/scale);
    } else {
      scale =1;
    }

    aux_img.create (rows_size, cols_size, CV_8UC3 );
    img_gray.create (rows_size, cols_size, CV_8UC1 );
  } catch (cv::Exception e) {
    GST_ERROR ("Size error");
    cvReleaseImage (&face_detect->priv->img_orig);
    face_detect->priv->img_orig = NULL;
    return;
  }

  Faces *faces = face_detect->priv->faces_detected;
  vector<Rect> *current_faces= new vector<Rect>;

  if ( ! __receive_event(face_detect,frame) && face_detect->priv->num_frames_to_process <= 0 )
    return;

  face_detect->priv->num_frame++;
  face_detect->priv->num_iter++;
  if ( (2 == face_detect->priv->process_x_every_4_frames &&
	(1 == face_detect->priv->num_frame % 2)) ||  
       ( (2 != face_detect->priv->process_x_every_4_frames) &&
	 (face_detect->priv->num_frame <= face_detect->priv->process_x_every_4_frames)))    
    {
      face_detect->priv->num_frames_to_process --;
      cv::resize( img,aux_img,  aux_img.size(), 0, 0, INTER_LINEAR );
      cvtColor( aux_img, img_gray, CV_BGR2GRAY );
      equalizeHist( img_gray, img_gray );
      
      face_detect->priv->cascade->detectMultiScale(img_gray,*current_faces,
			       MULTI_SCALE_FACTOR(face_detect->priv->scale_factor),
			       3,0,Size(img_gray.cols/20,img_gray.rows/20 ));

      if ( current_faces->size()  >0 ){
	Faces cf(*current_faces);
	faces->track_faces(&cf,face_detect->priv->track_threshold ,face_detect->priv->euclidean_threshold,face_detect->priv->area_threshold,face_detect->priv->num_iter);
      }
      else 
	{
	  if (face_detect->priv->frames_with_no_detection < MAX_NUM_FPS_WITH_NO_DETECTION)
	    face_detect->priv->frames_with_no_detection +=1;
	  else //delete all the faces
	    {
	      face_detect->priv->frames_with_no_detection =0;
	      faces->clear(); 	    	    
	    }
	} 
    }
  
  if (GOP == face_detect->priv->num_frame )
    face_detect->priv->num_frame=0;

  if (face_detect->priv->show_faces > 0) {
    if (face_detect->priv->costume != NULL) {
      vector<Rect> faces_vector;

      faces->get_faces (&faces_vector);

      for (vector<Rect>::iterator it = faces_vector.begin() ; it != faces_vector.end(); ++it) {

        kms_face_detect_display_detections_overlay_img (face_detect,
                                                        ((*it).x)*scale,
                                                        ((*it).y)*scale,
                                                        ((*it).width)*scale,
                                                        ((*it).height)*scale);
      }
    } else {
      faces->draw(face_detect->priv->img_orig,scale,face_detect->priv->num_iter);
    }

  }


}
/**
 * This function contains the image processing.
 */
static GstFlowReturn
kms_face_detect_transform_frame_ip (GstVideoFilter *filter,
				    GstVideoFrame *frame)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT (filter);
  GstMapInfo info;
  double scale=0.0;
  int width_to_process=0;
  int width=0,height=0;

  //struct timeval  start,end;

  //gettimeofday(&start,NULL);
  gst_buffer_map (frame->buffer, &info, GST_MAP_READ);	
  // setting up images

  KMS_FACE_DETECT_LOCK (face_detect);
  kms_face_detect_conf_images (face_detect, frame, info);

  scale = face_detect->priv->scale;
  width = face_detect->priv->img_width;
  height = face_detect->priv->img_height;
  width_to_process = face_detect->priv->width_to_process;

  kms_face_detect_process_frame(face_detect,width,height,scale,frame);

  kms_face_send_event(face_detect,frame,width_to_process);

  KMS_FACE_DETECT_UNLOCK (face_detect); 
    

  gst_buffer_unmap (frame->buffer, &info);

  //gettimeofday(&end,NULL);

  /* unsigned long long time_start= (((float)start.tv_sec * 1000.0) + (float(start.tv_usec)/1000.0));
  unsigned long long time_end=  (((float)end.tv_sec * 1000.0) + (float(end.tv_usec)/1000.0));
  time_events_ms
  unsigned long long total_time = time_end - time_start;*/
    
  return GST_FLOW_OK;
}

/*
 * In dispose(), you are supposed to free all types referenced from this
 * object which might themselves hold a reference to self. Generally,
 * the most simple solution is to unref all members on which you own a
 * reference.com

 * dispose() might be called multiple times, so we must guard against
 * calling g_object_unref() on an invalid GObject by setting the member
 * NULL; g_clear_object() does this for us, atomically.
 */
static void
kms_face_detect_dispose (GObject *object)
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
kms_face_detect_finalize (GObject *object)
{
  KmsFaceDetect *face_detect = KMS_FACE_DETECT(object);


  cvReleaseImage (&face_detect->priv->img_orig);

  if (face_detect->priv->cv_mem_storage != NULL)
    cvClearMemStorage (face_detect->priv->cv_mem_storage);
  if (face_detect->priv->face_seq != NULL)
    cvClearSeq (face_detect->priv->face_seq);

  cvReleaseMemStorage (&face_detect->priv->cv_mem_storage);

  if (face_detect->priv->costume != NULL)
    cvReleaseImage (&face_detect->priv->costume);

  if (face_detect->priv->image_to_overlay != NULL)
    gst_structure_free (face_detect->priv->image_to_overlay);

  if (face_detect->priv->dir_created) {
    remove_recursive (face_detect->priv->dir);
    g_free (face_detect->priv->dir);
  }

  delete face_detect->priv->faces_detected;
  //g_mutex_clear(&face_detect->priv->mutex);
  g_rec_mutex_clear(&face_detect->priv->mutex);
}

/*
 * In this function it is possible to initialize the variables.
 * For example, we set edge_value to 125 and the filter type to
 * edge filter. This values can be changed via set_properties
 */
static void
kms_face_detect_init (KmsFaceDetect *
		      face_detect)
  {
  int ret=0;
  face_detect->priv = KMS_FACE_DETECT_GET_PRIVATE (face_detect);
  face_detect->priv->scale=1.0;
  face_detect->priv->img_width = DEFAULT_WIDTH;
  face_detect->priv->img_height = DEFAULT_HEIGHT;
  face_detect->priv->img_orig = NULL;
  face_detect->priv->events_queue= g_queue_new();
  face_detect->priv->detect_event=0;
  face_detect->priv->meta_data=0;
  face_detect->priv->faces_detected= new Faces();
  face_detect->priv->num_frames_to_process=0;
  
  face_detect->priv->process_x_every_4_frames=PROCESS_ALL_FRAMES;
  face_detect->priv->num_frame=0;
  face_detect->priv->width_to_process=DEFAULT_WIDTH;
  face_detect->priv->scale_factor=DEFAULT_SCALE_FACTOR;
  face_detect->priv->euclidean_threshold = DEFAULT_EUCLIDEAN_DIS;
  face_detect->priv->track_threshold = TRACK_MAXIMUM_DISTANCE;
  face_detect->priv->area_threshold = AREA_THRESHOLD;
  face_detect->priv->num_iter = 0;
  face_detect->priv->frames_with_no_detection=0;
  face_detect->priv->server_events=SERVER_EVENTS;
  face_detect->priv->events_ms=EVENTS_MS;

  face_detect->priv->cv_mem_storage=cvCreateMemStorage(0);
  face_detect->priv->face_seq =cvCreateSeq (0, sizeof (CvSeq), sizeof (CvRect),
					    face_detect->priv->cv_mem_storage);
  face_detect->priv->show_faces = 0;
  
  kms_face_detect_init_cascade(face_detect);

  if (face_detect->priv->cascade == NULL)
    GST_ERROR ("Error reading the haar cascade configuration file");

  g_rec_mutex_init(&face_detect->priv->mutex);
	
}

static void
kms_face_detect_class_init (KmsFaceDetectClass *face)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (face);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (face);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template(GST_ELEMENT_CLASS (face),
				     gst_pad_template_new ("src", GST_PAD_SRC,
				     GST_PAD_ALWAYS,
				     gst_caps_from_string (VIDEO_SRC_CAPS)));
  gst_element_class_add_pad_template(GST_ELEMENT_CLASS (face),
				     gst_pad_template_new("sink", GST_PAD_SINK,
				     GST_PAD_ALWAYS,
				     gst_caps_from_string (VIDEO_SINK_CAPS)));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (face),
					 "face detection filter element", "Video/Filter",
					 "Fade detector",
					 "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = kms_face_detect_set_property;
  gobject_class->get_property = kms_face_detect_get_property;
  gobject_class->dispose = kms_face_detect_dispose;
  gobject_class->finalize = kms_face_detect_finalize;

  //properties definition
  g_object_class_install_property (gobject_class, PROP_VIEW_FACES,
				   g_param_spec_int ("view-faces", "view faces",
				  "To determine if we have to draw or hide the detected faces on the stream",
				   0, 1,FALSE, (GParamFlags) G_PARAM_READWRITE) );

  g_object_class_install_property (gobject_class, PROP_DETECT_BY_EVENT,
				   g_param_spec_int ("detect-event", "detect event",
						     "0 => Algorithm will be executed without constraints; 1 => the algorithm only will be executed for special event like motion detection",
				    0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SEND_META_DATA,
				   g_param_spec_int ("send-meta-data", "send meta data",
						     "0 (default) => it will not send meta data; 1 => it will send the bounding box of the face as metadata", 
				   0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));

g_object_class_install_property (gobject_class, PROP_WIDTH_TO_PROCCESS,
				  g_param_spec_int ("width-to-process", "width to process",
						     "160,320 (default),480,640 => this will be the width of the image that the algorithm is going to process to detect faces", 
				  0,640,FALSE, (GParamFlags) G_PARAM_READWRITE));

 g_object_class_install_property (gobject_class,   PROP_PROCESS_X_EVERY_4_FRAMES,
				  g_param_spec_int ("process-x-every-4-frames", "process x every 4 frames",
						    "1,2,3,4 (default) => process x frames every 4 frames", 
				  0,4,FALSE, (GParamFlags) G_PARAM_READWRITE));

 g_object_class_install_property (gobject_class,   PROP_EUCLIDEAN_THRESHOLD,
				  g_param_spec_int ("euclidean-distance", "euclidean distance",
						    "0 - 20 (8 default) => Distance among faces of consecutives faces to delete vibrations produced by little changes of pixels of the same faces", 
				  0,20,FALSE, (GParamFlags) G_PARAM_READWRITE));


 g_object_class_install_property (gobject_class,   PROP_TRACK_THRESHOLD,
				  g_param_spec_int ("track-threshold", "track threshold",
						    "0 - 100 (30 default)", 
                                  0,100,FALSE, (GParamFlags) G_PARAM_READWRITE)); 

g_object_class_install_property (gobject_class,   PROP_AREA_THRESHOLD,
				  g_param_spec_int ("area-threshold", "area threshold",
						    "0 - 1000 (500 default)", 
                                  0,1000,FALSE, (GParamFlags) G_PARAM_READWRITE)); 

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
    GST_DEBUG_FUNCPTR (kms_face_detect_transform_frame_ip);


  kms_face_detector_signals[SIGNAL_ON_FACE_EVENT] =
    g_signal_new ("face-event",
		  G_TYPE_FROM_CLASS (face),
		  G_SIGNAL_RUN_LAST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 1, G_TYPE_STRING);
  
  g_type_class_add_private (face, sizeof (KmsFaceDetectPrivate) );  

  face->base_face_detect_class.parent_class.sink_event =
    GST_DEBUG_FUNCPTR(kms_face_detect_sink_events);

}

gboolean
kms_face_detect_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               KMS_TYPE_FACE_DETECT);
}
