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

#ifndef __GST_MPEGH3DAHASH_H__
#define __GST_MPEGH3DAHASH_H__

#include <gst/gst.h>
#include "gstmpegh3dacommon.h"

G_BEGIN_DECLS

/* Incremental digest hasher (SHA-1/256/384/512), GLib GChecksum backed.
 * SHA-224 is not implemented yet. */
typedef struct _GstMpegh3daHash GstMpegh3daHash;

GstMpegh3daHash *gst_mpegh3da_hash_new (GstMpegh3daHashMethod method);
void gst_mpegh3da_hash_free (GstMpegh3daHash * hash);
gboolean gst_mpegh3da_hash_update (GstMpegh3daHash * hash,
    const guint8 * data, gsize size);
gboolean gst_mpegh3da_hash_finish (GstMpegh3daHash * hash, guint8 * digest,
    gsize * digest_size);
gsize gst_mpegh3da_hash_digest_size (GstMpegh3daHashMethod method);

G_END_DECLS

#endif /* __GST_MPEGH3DAHASH_H__ */
