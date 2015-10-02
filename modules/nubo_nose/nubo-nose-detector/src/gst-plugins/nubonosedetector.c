
#include <config.h>
#include <gst/gst.h>

#include "kmsnosedetect.h"

static gboolean
init (GstPlugin * plugin)
{
  if (!kms_nose_detect_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nubonosedetector,
    "Kurento nose detector filter",
    init, VERSION, "LGPL", "Kurento", "http://kurento.com/")
