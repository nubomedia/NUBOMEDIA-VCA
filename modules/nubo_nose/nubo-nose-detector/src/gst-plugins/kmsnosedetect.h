#ifndef _KMS_NOSE_DETECT_H_
#define _KMS_NOSE_DETECT_H_

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define KMS_TYPE_NOSE_DETECT   (kms_nose_detect_get_type())
#define KMS_NOSE_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),KMS_TYPE_NOSE_DETECT,KmsNoseDetect))
#define KMS_NOSE_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),KMS_TYPE_NOSE_DETECT,KmsNoseDetectClass))
#define KMS_IS_NOSE_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),KMS_TYPE_NOSE_DETECT))
#define KMS_IS_NOSE_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),KMS_TYPE_NOSE_DETECT))
typedef struct _KmsNoseDetect KmsNoseDetect;
typedef struct _KmsNoseDetectClass KmsNoseDetectClass;
typedef struct _KmsNoseDetectPrivate KmsNoseDetectPrivate;

typedef enum
  {
    KMS_NOSE_DETECT_TYPE_EDGES,
    KMS_NOSE_DETECT_TYPE_GREY,
  }
  KmsNoseDetectType;

struct _KmsNoseDetect
{
  GstVideoFilter base;
  KmsNoseDetectPrivate *priv;
};

struct _KmsNoseDetectClass
{
  GstVideoFilterClass base_nose_detect_class;
};

GType kms_nose_detect_get_type (void);

gboolean kms_nose_detect_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* _KMS_nose_detect_H_ */
