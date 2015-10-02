
#include "nubo_face_meta.h"

GType nubo_face_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "id", "val", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("NuboFaceMetaApi", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
nubo_face_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  NuboFaceMeta *emeta = (NuboFaceMeta *) meta;

  emeta->p1_x = 0;
  emeta->p1_y = 0;
  
  emeta->p2_x=0;
  emeta->p2_y=0;
  emeta->name = NULL;

  return TRUE;
}
 
static gboolean
nubo_face_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  NuboFaceMeta *emeta = (NuboFaceMeta *) meta;
  
  /* we always copy no matter what transform */
  gst_buffer_add_nubo_face_meta (transbuf, emeta->p1_x, emeta->p1_y, 
				  emeta->p2_x, emeta->p2_y, emeta->name);
  
  printf("Nothing to do at the moment .... \n");

  return TRUE;
}

static void
nubo_face_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  NuboFaceMeta *emeta = (NuboFaceMeta *) meta;

  //if we have to release some pointers...
  g_free (emeta->name);
  emeta->name = NULL;
}

const GstMetaInfo *
nubo_face_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (NUBO_FACE_META_API_TYPE,
        "NuboFaceMeta",
        sizeof (NuboFaceMeta),
        nubo_face_meta_init,
        nubo_face_meta_free,
        nubo_face_meta_transform);
    g_once_init_leave (&meta_info, mi);
  }
  return meta_info;
}

NuboFaceMeta *gst_buffer_add_nubo_face_meta (GstBuffer   *buffer,
					     gint         p1_x,
					      gint         p1_y,
					      gint         p2_x,
					      gint         p2_y,
					      gchar  *name	     
					      )
{
  NuboFaceMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = (NuboFaceMeta *) gst_buffer_add_meta (buffer,
      NUBO_FACE_META_INFO, NULL);

  meta->p1_x = p1_x;
  meta->p1_y = p1_y;
  
  meta->p2_x = p2_x;
  meta->p2_y = p2_y;

  meta->name = g_strdup(name);
  
  return meta;
}
