/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Kurento (http://kurento.org/)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * cop ies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _KMS_EAR_DETECT_H_
#define _KMS_EAR_DETECT_H_

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define KMS_TYPE_EAR_DETECT   (kms_ear_detect_get_type())
#define KMS_EAR_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),KMS_TYPE_EAR_DETECT,KmsEarDetect))
#define KMS_EAR_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),KMS_TYPE_EAR_DETECT,KmsEarDetectClass))
#define KMS_IS_EAR_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),KMS_TYPE_EAR_DETECT))
#define KMS_IS_EAR_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),KMS_TYPE_EAR_DETECT))
typedef struct _KmsEarDetect KmsEarDetect;
typedef struct _KmsEarDetectClass KmsEarDetectClass;
typedef struct _KmsEarDetectPrivate KmsEarDetectPrivate;

typedef enum
{
  KMS_EAR_DETECT_TYPE_EDGES,
  KMS_EAR_DETECT_TYPE_GREY,
}
KmsEarDetectType;

struct _KmsEarDetect
{
  GstVideoFilter base;
  KmsEarDetectPrivate *priv;
};

struct _KmsEarDetectClass
{
  GstVideoFilterClass base_ear_detect_class;
};

GType kms_ear_detect_get_type (void);

gboolean kms_ear_detect_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* _KMS_ear_detect_H_ */
