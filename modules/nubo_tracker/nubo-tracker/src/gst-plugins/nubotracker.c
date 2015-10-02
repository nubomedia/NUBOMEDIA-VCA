#include <config.h>
#include <gst/gst.h>

#include "gstnubotracker.h"

static gboolean
init (GstPlugin * plugin)
{
  if (!gst_nubo_tracker_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nubotracker,
    "Filter documentation",
    init, VERSION, GST_LICENSE_UNKNOWN, "PACKAGE_NAME", "origin")
