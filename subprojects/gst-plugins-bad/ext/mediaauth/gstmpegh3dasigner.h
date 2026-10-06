/*
 * Copyright (C) 2026 Fluendo
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_MPEGH3DASIGNER_H__
#define __GST_MPEGH3DASIGNER_H__

#include <gst/base/gstbasetransform.h>
#include "gstmpegh3dacommon.h"
#include "gstmpegh3dahash.h"
#include "gstmpegh3daauthpktsbuild.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEGH3DASIGNER (gst_mpegh3dasigner_get_type ())
#define GST_MPEGH3DASIGNER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPEGH3DASIGNER, GstMpegh3DASigner))
#define GST_MPEGH3DASIGNER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPEGH3DASIGNER, GstMpegh3DASignerClass))
#define GST_IS_MPEGH3DASIGNER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPEGH3DASIGNER))
#define GST_IS_MPEGH3DASIGNER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPEGH3DASIGNER))

typedef struct _GstMpegh3DASigner GstMpegh3DASigner;
typedef struct _GstMpegh3DASignerClass GstMpegh3DASignerClass;

struct _GstMpegh3DASigner
{
  GstBaseTransform parent;

  /* properties */
  GstMpegh3daHashMethod hash_method;
  guint auth_sequence_length;
  guint8 auth_id;
  gboolean au_counter;
  gboolean timestamp;

  gchar *content_uuid;          /* property: RFC 9562 UUID string */
  guint8 uuid[16];              /* parsed 16-byte UUID */
  gboolean uuid_set;

  /* authentication sequence state */
  GstMpegh3daHash *hash;        /* hasher for the current sequence */
  guint64 seq_label;            /* MHASPacketLabel of the sequence */
  gboolean have_seq_label;
  guint frame_count;            /* AUs hashed in the current sequence */
  gboolean sequence_finished;   /* previous sequence ended; start a new one */
};

struct _GstMpegh3DASignerClass
{
  GstBaseTransformClass parent_class;
};

GType gst_mpegh3dasigner_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (mpegh3dasigner);

G_END_DECLS

#endif /* __GST_MPEGH3DASIGNER_H__ */
