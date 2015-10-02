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

#ifndef _KMS_MOUTH_DETECT_H_
#define _KMS_MOUTH_DETECT_H_

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define KMS_TYPE_MOUTH_DETECT   (kms_mouth_detect_get_type())
#define KMS_MOUTH_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),KMS_TYPE_MOUTH_DETECT,KmsMouthDetect))
#define KMS_MOUTH_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),KMS_TYPE_MOUTH_DETECT,KmsMouthDetectClass))
#define KMS_IS_MOUTH_DETECT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),KMS_TYPE_MOUTH_DETECT))
#define KMS_IS_MOUTH_DETECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),KMS_TYPE_MOUTH_DETECT))
typedef struct _KmsMouthDetect KmsMouthDetect;
typedef struct _KmsMouthDetectClass KmsMouthDetectClass;
typedef struct _KmsMouthDetectPrivate KmsMouthDetectPrivate;

typedef enum
{
  KMS_MOUTH_DETECT_TYPE_EDGES,
  KMS_MOUTH_DETECT_TYPE_GREY,
}
KmsMouthDetectType;

struct _KmsMouthDetect
{
  GstVideoFilter base;
  KmsMouthDetectPrivate *priv;
};

struct _KmsMouthDetectClass
{
  GstVideoFilterClass base_mouth_detect_class;
};

GType kms_mouth_detect_get_type (void);

gboolean kms_mouth_detect_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* _KMS_face_detect_H_ */
