
#ifndef _KMS_EYE_DETECT_H_
#define _KMS_EYE_DETECT_H_

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define KMS_TYPE_EYE_DETECT   (kms_eye_detect_get_type())
#define KMS_EYE_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),KMS_TYPE_EYE_DETECT,KmsEyeDetect))
#define KMS_EYE_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),KMS_TYPE_EYE_DETECT,KmsEyeDetectClass))
#define KMS_IS_EYE_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),KMS_TYPE_EYE_DETECT))
#define KMS_IS_EYE_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),KMS_TYPE_EYE_DETECT))
typedef struct _KmsEyeDetect KmsEyeDetect;
typedef struct _KmsEyeDetectClass KmsEyeDetectClass;
typedef struct _KmsEyeDetectPrivate KmsEyeDetectPrivate;

typedef enum
{
  KMS_EYE_DETECT_TYPE_EDGES,
  KMS_EYE_DETECT_TYPE_GREY,
}
KmsEyeDetectType;

struct _KmsEyeDetect
{
  GstVideoFilter base;
  KmsEyeDetectPrivate *priv;
};

struct _KmsEyeDetectClass
{
  GstVideoFilterClass base_eye_detect_class;
};

GType kms_eye_detect_get_type (void);

gboolean kms_eye_detect_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* _KMS_face_detect_H_ */
