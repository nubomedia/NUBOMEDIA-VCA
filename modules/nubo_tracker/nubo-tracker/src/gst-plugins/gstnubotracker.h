#ifndef _GST_NUBO_TRACKER_H_
#define _GST_NUBO_TRACKER_H_

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define GST_TYPE_NUBO_TRACKER   (gst_nubo_tracker_get_type())
#define GST_NUBO_TRACKER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NUBO_TRACKER,GstNuboTracker))
#define GST_NUBO_TRACKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NUBO_TRACKER,GstNuboTrackerClass))
#define GST_IS_NUBO_TRACKER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NUBO_TRACKER))
#define GST_IS_NUBO_TRACKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NUBO_TRACKER))
typedef struct _GstNuboTracker GstNuboTracker;
typedef struct _GstNuboTrackerClass GstNuboTrackerClass;
typedef struct _GstNuboTrackerPrivate GstNuboTrackerPrivate;

struct _GstNuboTracker
{
  GstVideoFilter base;
  GstNuboTrackerPrivate *priv;
};

struct _GstNuboTrackerClass
{
  GstVideoFilterClass base_nubo_tracker_class;
};

GType gst_nubo_tracker_get_type (void);

gboolean gst_nubo_tracker_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* _GST_NUBO_TRACKER_H_ */
