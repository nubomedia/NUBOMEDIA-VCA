
#include <config.h>
#include <gst/gst.h>

#include "kmseyedetect.h"

static gboolean
init (GstPlugin * plugin)
{
  if (!kms_eye_detect_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    eyefilter,
    "Kurento eye detector filter",
    init, VERSION, "LGPL", "Kurento", "http://kurento.com/")
