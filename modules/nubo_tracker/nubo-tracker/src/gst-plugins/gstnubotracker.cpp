#include "gstnubotracker.h"

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


#define PLUGIN_NAME "nubotracker"
#define DEFAULT_THRESHOLD 20
#define DEFAULT_MIN_AREA 50
#define DEFAULT_MAX_AREA 30000
#define DEFAULT_DISTANCE 35

#define MHI_DURATION  0.2
#define MAX_TIME_DELTA  0.5
#define MIN_TIME_DELTA  0.05
#define SEGMENTATION 32
#define SERVER_EVENTS 0
#define EVENTS_MS 30001

using namespace cv;


#define GST_NUBO_TRACKER_LOCK(nubo_tracker)				\
  (g_rec_mutex_lock (&( (GstNuboTracker *) nubo_tracker)->priv->mutex))

#define GST_NUBO_TRACKER_UNLOCK(nubo_tracker)				\
  (g_rec_mutex_unlock (&( (GstNuboTracker *) nubo_tracker)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (gst_nubo_tracker_debug_category);
#define GST_CAT_DEFAULT gst_nubo_tracker_debug_category


#define GST_NUBO_TRACKER_GET_PRIVATE(obj) (	\
    G_TYPE_INSTANCE_GET_PRIVATE (               \
        (obj),                                  \
        GST_TYPE_NUBO_TRACKER,                 \
        GstNuboTrackerPrivate                  \
						)	\
					       )

/* pad templates */
#define VIDEO_SRC_CAPS \
  GST_VIDEO_CAPS_MAKE("{ BGRA }")

#define VIDEO_SINK_CAPS \
  GST_VIDEO_CAPS_MAKE("{ BGRA }")

/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstNuboTracker, gst_nubo_tracker,
                         GST_TYPE_VIDEO_FILTER,
                         GST_DEBUG_CATEGORY_INIT (gst_nubo_tracker_debug_category,
                             PLUGIN_NAME, 0,
                             "debug category for nubo_tracker element") );
enum {
  PROP_0,
  PROP_THRESHOLD,
  PROP_MIN_AREA,
  PROP_MAX_AREA,
  PROP_DISTANCE,
  PROP_ACTIVATE_SERVER_EVENTS,
  PROP_SERVER_EVENTS_MS,
  PROP_VISUAL_MODE
};


enum {
  SIGNAL_ON_TRACKER_EVENT,
  LAST_SIGNAL
};

static guint kms_tracker_detector_signals[LAST_SIGNAL] = { 0 };

struct _GstNuboTrackerPrivate {    
  IplImage *img_orig;
  Mat *img_prev;
  Mat *motion_history;

  int threshold;
  int min_area;
  long max_area;
  int distance;
  int visual_mode;
  int img_height;
  int img_width;
  int num_frames;
  int server_events;
  int events_ms;
  double time_events_ms;
  GRecMutex mutex;
  gboolean debug;
};

static Mat img_prev;


template<typename T>
string toString(const T& value)
{
  std::stringstream ss;
   ss << value;
   return ss.str();
}

static float calc_dist(Point p1, int p1_width, int p1_height ,Point p2, int p2_width, int p2_height)
{
  Point c1,c2;
  c1.x= p1.x + p1_width/2;
  c1.y= p1.y + p1_height/2;
  c2.x= p2.x + p2_width/2;
  c2.y= p2.y + p2_height/2;

  return sqrt((c1.x-c2.x)*(c1.x-c2.x) + (c1.y-c2.y)*(c1.y-c2.y));

}

static Rect __merge(Rect r1,Rect r2)
{
  Point tl1,tl2;
  Point br1,br2;
  Point res_tl,res_br;
  
  tl1=r1.tl();
  tl2=r2.tl();
  br1=r1.br();
  br2=r2.br();
  
  if (tl2.inside(r1) && br2.inside(r1))
    return r1;
  
  if (tl1.inside(r2) && br1.inside(r2))
    return r2;

  if (tl1.x < tl2.x)  
    res_tl.x=tl1.x;
  else 
    res_tl.x=tl2.x;

  if (tl1.y < tl2.y)  
    res_tl.y=tl1.y;
  else 
    res_tl.y=tl2.y;
 
  if (br1.x > br2.x)  
    res_br.x=br1.x;
  else 
    res_br.x=br2.x;
  
  if (br1.y > br2.y)  
    res_br.y=br1.y;
  else 
    res_br.y=br2.y;
  
  return Rect(res_tl,res_br);
}

static int __join_objects(GstNuboTracker *tracker,vector<Rect> &seg_bounds)
{
  //Merging squares
  for (int a=seg_bounds.size()-1; a>=0;a--)
    {		    

      if (seg_bounds[a].area() > tracker->priv->min_area
	  && seg_bounds[a].area() < tracker->priv->max_area)		      

	for (int b=a-1;b>=0;b--)
	  {			
	    if (seg_bounds[b].area() > tracker->priv->min_area
		&& seg_bounds[b].area() < tracker->priv->max_area)	      
	      
	      if ( tracker->priv->distance > 
		   calc_dist(seg_bounds[a].tl(),seg_bounds[a].width,seg_bounds[a].height,
			     seg_bounds[b].tl(),seg_bounds[b].width,seg_bounds[b].height))
		{
		  Rect r = __merge(seg_bounds[a],seg_bounds[b]); 
		  seg_bounds.at(b) = r;
		  seg_bounds.erase(seg_bounds.begin()+a);
		  break;  
		}							
	  }
      else      
	seg_bounds.erase(seg_bounds.begin()+a);      
    }
 
  return 0;
}

static int
gst_nubo_tracker_img_conf (GstNuboTracker *tracker,
			   GstVideoFrame *frame, GstMapInfo &info)
{
  
  if (tracker->priv->img_orig == NULL) {
    
    tracker->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
    						frame->info.height),
    						 IPL_DEPTH_8U, 4);
     
    tracker->priv->motion_history = new Mat(frame->info.height,
					    frame->info.width,CV_32FC1,Scalar(0,0,0));
    
  } else if ( (tracker->priv->img_orig->width != frame->info.width)
              || (tracker->priv->img_orig->height != frame->info.height) ) {
    
    cvReleaseImage(&tracker->priv->img_orig);
    delete tracker->priv->motion_history;

    tracker->priv->img_orig= cvCreateImageHeader(cvSize(frame->info.width,
    							frame->info.height),
    						 IPL_DEPTH_8U, 4);

    tracker->priv->motion_history = new Mat(frame->info.height,
					    frame->info.width,CV_32FC1,Scalar(0,0,0));

  } 

  tracker->priv->img_orig->imageData = (char *)info.data;
  tracker->priv->img_height=frame->info.height;
  tracker->priv->img_width=frame->info.width;

  return 0;

}

static void
gst_nubo_tracker_set_property (GObject *object, guint property_id,
			       const GValue *value, GParamSpec *pspec)
{
  GstNuboTracker *nubo_tracker = GST_NUBO_TRACKER (object);
  //Changing values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  struct timeval  t;
  GST_NUBO_TRACKER_LOCK (nubo_tracker);

  switch (property_id) {
  
  case PROP_THRESHOLD:
    nubo_tracker->priv->threshold = g_value_get_int (value);
    break;    

  case PROP_MIN_AREA:
    nubo_tracker->priv->min_area =  g_value_get_int(value);
    
    break;

  case PROP_MAX_AREA:
    nubo_tracker->priv->max_area =  g_value_get_long(value);
    break;
    
  case PROP_DISTANCE:
    nubo_tracker->priv->distance = g_value_get_int(value);
    break;

  case PROP_VISUAL_MODE:
    nubo_tracker->priv->visual_mode = g_value_get_int(value);
    break;
    
  case  PROP_ACTIVATE_SERVER_EVENTS:
    nubo_tracker->priv->server_events = g_value_get_int(value);
    gettimeofday(&t,NULL);
    nubo_tracker->priv->time_events_ms= ((t.tv_sec * 1000.0) + ((t.tv_usec)/1000.0));    
    break;
    
  case PROP_SERVER_EVENTS_MS:
    nubo_tracker->priv->events_ms = g_value_get_int(value);
    break;   
    
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }

  GST_NUBO_TRACKER_UNLOCK (nubo_tracker);
}

static void
gst_nubo_tracker_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
  GstNuboTracker *nubo_tracker = GST_NUBO_TRACKER (object);
  //Reading values of the properties is a critical region because read/write
  //concurrently could produce race condition. For this reason, the following
  //code is protected with a mutex
  GST_NUBO_TRACKER_LOCK (nubo_tracker);

  switch (property_id) {

  case PROP_THRESHOLD:
    g_value_set_int (value, nubo_tracker->priv->threshold);
    break;
    
  case PROP_MIN_AREA:
    g_value_set_int(value,nubo_tracker->priv->min_area);
    break;
    
  case PROP_MAX_AREA:
    g_value_set_long(value,nubo_tracker->priv->max_area);
    break;
    
  case PROP_DISTANCE:
    g_value_set_int(value,nubo_tracker->priv->distance);
    break;    
    
  case PROP_VISUAL_MODE:
    g_value_set_int(value,nubo_tracker->priv->visual_mode);
    break;
    
  case  PROP_ACTIVATE_SERVER_EVENTS:
    g_value_set_int(value,nubo_tracker->priv->server_events);
    break;
    
  case PROP_SERVER_EVENTS_MS:
    g_value_set_int(value,nubo_tracker->priv->events_ms);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
  GST_NUBO_TRACKER_UNLOCK (nubo_tracker);

}

static void gst_nubo_tracker_process(GstNuboTracker *tracker)
{

  Mat img_orig( tracker->priv->img_orig );
  Mat gray(tracker->priv->img_height, tracker->priv->img_width,CV_8UC1);
  Mat frame_diff(tracker->priv->img_height, tracker->priv->img_width,CV_8UC1);
  Mat ret,motion_mask;
  Mat mg_mask(tracker->priv->img_height, tracker->priv->img_width, CV_8UC1,Scalar(0,0,0));
  Mat mg_orient(tracker->priv->img_height, tracker->priv->img_width, CV_32FC1,Scalar(0,0,0));
  Mat seg_mask(tracker->priv->img_height, tracker->priv->img_width, CV_32FC1,Scalar(0,0,0));
  double timestamp = 1000.0*clock()/CLOCKS_PER_SEC;
  vector<Rect> seg_bounds;
  ret=(img_orig.clone());
  std::string tracker_str;
  struct timeval  end; 
  double current_t, diff_time;
 
  cvtColor(img_orig,gray,CV_BGR2GRAY);
  
  if (tracker->priv->num_frames > 0)
    {
      //getting the image difference
      absdiff(gray,img_prev,frame_diff);
      
      //thresholding
      threshold(frame_diff,ret,tracker->priv->threshold,255,0);
 
      motion_mask=ret.clone();
      //update motion
      updateMotionHistory(motion_mask, *(tracker->priv->motion_history), 
			  timestamp, MHI_DURATION);
      
      //calc gradient
      calcMotionGradient(*(tracker->priv->motion_history), mg_mask, mg_orient, 
			 MIN_TIME_DELTA, MAX_TIME_DELTA, 3);

      //segmentation
      segmentMotion(*(tracker->priv->motion_history), seg_mask, seg_bounds,
		    timestamp, SEGMENTATION);
      
      //joing object based on the distance
      ret=__join_objects(tracker,seg_bounds);
      
      //draw rectangles
      if (tracker->priv->visual_mode > 0 || 1 == tracker->priv->server_events)
	for (unsigned int h = 0; h < seg_bounds.size();h++)
	{
	 	  
	  Rect rec = seg_bounds[h];
	  if (tracker->priv->visual_mode > 0)
	    cvRectangle(tracker->priv->img_orig,rec.tl(),rec.br(),Scalar(0,0,255),3,8,0);

	  if (1 == tracker->priv->server_events)
	    {
	      std::string new_obj ("x:" + toString((guint)  rec.x) + 
				   ",y:" + toString((guint) rec.y ) + 
				   ",width:" + toString((guint) rec.width )+ 
				   ",height:" + toString((guint) rec.height )+ ";");
	      tracker_str= tracker_str + new_obj;
	    }
	}
    }   
  
  if (seg_bounds.size()>0)
    {
      gettimeofday(&end,NULL);
      current_t= ((end.tv_sec * 1000.0) + ((end.tv_usec)/1000.0));
      diff_time = current_t - tracker->priv->time_events_ms;
      
      if (1 == tracker->priv->server_events && diff_time > tracker->priv->events_ms)
	{
	  tracker->priv->time_events_ms=current_t;
	  g_signal_emit (G_OBJECT (tracker),
			 kms_tracker_detector_signals[SIGNAL_ON_TRACKER_EVENT], 0,tracker_str.c_str());
	}
    }
  if (!(img_prev.empty()))
    img_prev.release();
  

  img_prev = gray.clone();
        
}

static GstFlowReturn
gst_nubo_tracker_transform_frame_ip (GstVideoFilter *filter,
                                      GstVideoFrame *frame)
{
  GstNuboTracker  *tracker = GST_NUBO_TRACKER (filter);
  GstMapInfo info;
  Mat aux;

  gst_buffer_map( frame->buffer, &info, GST_MAP_READ);
  GST_NUBO_TRACKER_LOCK (tracker);

  gst_nubo_tracker_img_conf (tracker, frame, info);

  gst_nubo_tracker_process(tracker);
  
  tracker->priv->num_frames++;
  
  GST_NUBO_TRACKER_UNLOCK (tracker);
  
  gst_buffer_unmap(frame->buffer, &info);
  
  return GST_FLOW_OK;
}

static void
gst_nubo_tracker_finalize (GObject *object)
{
}

static void
gst_nubo_tracker_init (GstNuboTracker *nubo_tracker)
{

 nubo_tracker->priv = GST_NUBO_TRACKER_GET_PRIVATE (nubo_tracker); 
 nubo_tracker->priv->threshold=DEFAULT_THRESHOLD;
 nubo_tracker->priv->min_area=DEFAULT_MIN_AREA;
 nubo_tracker->priv->max_area=DEFAULT_MAX_AREA;
 nubo_tracker->priv->distance=DEFAULT_DISTANCE;
 nubo_tracker->priv->num_frames=0;
 nubo_tracker->priv->visual_mode=0;
 nubo_tracker->priv->img_orig=NULL;
 
 nubo_tracker->priv->server_events=SERVER_EVENTS;
 nubo_tracker->priv->events_ms=EVENTS_MS;
 //nubo_tracker->priv->img_prev=NULL;

}

static void
gst_nubo_tracker_dispose (GObject *object)
{
}

static void
gst_nubo_tracker_class_init (GstNuboTrackerClass *tracker)
{

  GObjectClass *gobject_class = G_OBJECT_CLASS (tracker);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (tracker);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, PLUGIN_NAME, 0, PLUGIN_NAME);

  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (tracker),
                                      gst_pad_template_new ("src", GST_PAD_SRC,
                                          GST_PAD_ALWAYS,
                                          gst_caps_from_string (VIDEO_SRC_CAPS) ) );
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (tracker),
                                      gst_pad_template_new ("sink", GST_PAD_SINK,
                                          GST_PAD_ALWAYS,
                                          gst_caps_from_string (VIDEO_SINK_CAPS) ) );

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (tracker),
                                      "Motion tracker filter element", "Video/Filter",
                                      "Motion Tracker",
                                      "Victor Manuel Hidalgo <vmhidalgo@visual-tools.com>");

  gobject_class->set_property = gst_nubo_tracker_set_property;
  gobject_class->get_property = gst_nubo_tracker_get_property;
  gobject_class->dispose = gst_nubo_tracker_dispose;
  gobject_class->finalize = gst_nubo_tracker_finalize;

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
				   g_param_spec_int ("set_threshold", "set_threshold",
						     "To set up the minumum difference among pixels to consider motion",
						     0,255, FALSE,
						     (GParamFlags) (G_PARAM_READWRITE) ) );
  
  g_object_class_install_property (gobject_class, PROP_MIN_AREA,
				   g_param_spec_int ("set_min_area", "set_min_area",
						     "To set up the minumum area to consider objects",
						     0,10000, FALSE,
						     (GParamFlags) (G_PARAM_READWRITE) ) );  
  
  g_object_class_install_property (gobject_class, PROP_MAX_AREA,
				   g_param_spec_long ("set_max_area", "set_max_area",
						      "To set up the maximum area to consider objects",
						      0,300000, FALSE,
						      (GParamFlags) (G_PARAM_READWRITE) ) );  
  
  g_object_class_install_property (gobject_class, PROP_DISTANCE,
				   g_param_spec_int ("set_distance", "set_distance",
						     "To set up the distance among object to merge them",
						     0,2000, FALSE,
						     (GParamFlags) (G_PARAM_READWRITE) ) );  

  g_object_class_install_property (gobject_class, PROP_VISUAL_MODE,
				   g_param_spec_int ("set_visual_mode", "set_visual_mode",
						     "To set up the visual mode of the video output0=>normal;1=>see objects; 2 => objects + image diff, 3=>object + motion histogram",
						     0,4, FALSE,
						     (GParamFlags) (G_PARAM_READWRITE) ) );  
  
  g_object_class_install_property (gobject_class,   PROP_ACTIVATE_SERVER_EVENTS,
				   g_param_spec_int ("activate-events", "Activate Events",
						     "(0 default) => It will not send events to server, 1 => it will send events to the server", 
						     0,1,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,   PROP_SERVER_EVENTS_MS,
				   g_param_spec_int ("events-ms",  "Activate Events",
						     "the time, it takes to send events to the servers", 
						     0,30000,FALSE, (GParamFlags) G_PARAM_READWRITE));
  
  video_filter_class->transform_frame_ip =
    GST_DEBUG_FUNCPTR (gst_nubo_tracker_transform_frame_ip);
  
  kms_tracker_detector_signals[SIGNAL_ON_TRACKER_EVENT] =
    g_signal_new ("tracker-event",
		  G_TYPE_FROM_CLASS (tracker),
		  G_SIGNAL_RUN_LAST,
		  0, NULL, NULL, NULL,
		  G_TYPE_NONE, 1, G_TYPE_STRING);

  g_type_class_add_private (tracker, sizeof (GstNuboTrackerPrivate) );
  

}

gboolean
gst_nubo_tracker_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
                               GST_TYPE_NUBO_TRACKER);
}
