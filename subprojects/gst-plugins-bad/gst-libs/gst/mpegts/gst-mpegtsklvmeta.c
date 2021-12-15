/* Gstreamer
 * Copyright 2021 Brad Hards <bradh@frogmouth.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst-mpegtsklvmeta.h"

#define GST_CAT_DEFAULT mpegts_debug

static gboolean
gst_mpegts_klv_meta_init (GstMpegtsKlvMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  return TRUE;
}

static void
gst_mpegts_klv_meta_free (GstMpegtsKlvMeta * meta, GstBuffer * buffer)
{
}

static gboolean
gst_mpegts_klv_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstMpegtsKlvMeta *source_meta, *dest_meta;

  source_meta = (GstMpegtsKlvMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy = data;
    if (!copy->region) {
      dest_meta = gst_buffer_add_mpegts_klv_meta (dest);
      if (!dest_meta)
        return FALSE;
      dest_meta->metadata_service_id = source_meta->metadata_service_id;
      dest_meta->sequence_number = source_meta->sequence_number;
      dest_meta->flags = source_meta->flags;
    }
  } else {
    /* return FALSE, if transform type is not supported */
    return FALSE;
  }
  return TRUE;
}

GType
gst_mpegts_klv_meta_api_get_type (void)
{
  static GType type;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstMpegtsKlvMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_mpegts_klv_meta_get_info (void)
{
  static const GstMetaInfo *mpegts_klv_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & mpegts_klv_meta_info)) {
    const GstMetaInfo *meta = gst_meta_register (GST_MPEGTS_KLV_META_API_TYPE,
        "GstMpegtsKlvMeta", sizeof (GstMpegtsKlvMeta),
        (GstMetaInitFunction) gst_mpegts_klv_meta_init,
        (GstMetaFreeFunction) gst_mpegts_klv_meta_free,
        (GstMetaTransformFunction) gst_mpegts_klv_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & mpegts_klv_meta_info,
        (GstMetaInfo *) meta);
  }

  return mpegts_klv_meta_info;
}

GstMpegtsKlvMeta *
gst_buffer_add_mpegts_klv_meta (GstBuffer * buffer)
{
  GstMpegtsKlvMeta *meta;
  meta =
      (GstMpegtsKlvMeta *) gst_buffer_add_meta (buffer,
      GST_MPEGTS_KLV_META_INFO, NULL);
  return meta;
}
