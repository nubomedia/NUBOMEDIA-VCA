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

#ifndef _GST_META_FACE_H_
#define _GST_META_FACE_H_

#include <gst/video/gstvideometa.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <glib/gstdio.h>

typedef struct __NuboFaceMeta NuboFaceMeta;

struct __NuboFaceMeta {
  
  GstMeta meta;
  
  gint p1_x; 
  gint p2_x;
  gint p1_y;
  gint p2_y;
  gchar *name;
};

//registering out metadata API definition	
GType nubo_face_meta_api_get_type (void);
#define NUBO_FACE_META_API_TYPE (nubo_face_meta_api_get_type())

//finds and returns the metadata with our new API.
#define gst_buffer_get_nubo_face_meta(b) \
  ((NuboFaceMeta*)gst_buffer_get_meta((b),NUBO_FACE_META_API_TYPE))
  
  
const GstMetaInfo *nubo_face_meta_get_info (void);
#define NUBO_FACE_META_INFO (nubo_face_meta_get_info())

NuboFaceMeta *gst_buffer_add_nubo_face_meta (GstBuffer      *buffer,
					      gint            p1_x,
                                              gint	      p1_y,
					      gint	      p2_x,
					      gint	      p2_y,
					      gchar 	      *name );

#endif //_GST_META_FACE_H_