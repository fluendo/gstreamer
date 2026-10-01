/*
 * Copyright (C) 2026 Dolby Laboratories, Inc.
 * Copyright (C) 2026 Pablo García <pgarcia@fluendo.com>
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

#ifndef __GST_MPEGH3DAENC_H__
#define __GST_MPEGH3DAENC_H__

#include <gst/audio/gstaudioencoder.h>

G_BEGIN_DECLS

#define GST_TYPE_MPEGH3DAENC (gst_mpegh3daenc_get_type ())
#define GST_MPEGH3DAENC(obj)                                                  \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPEGH3DAENC, GstMpegH3DEnc))
#define GST_MPEGH3DAENC_CLASS(klass)                                          \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPEGH3DAENC, GstMpegH3DEncClass))
#define GST_IS_MPEGH3DAENC(obj)                                               \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPEGH3DAENC))
#define GST_IS_MPEGH3DAENC_CLASS(klass)                                       \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPEGH3DAENC))

typedef struct _GstMpegH3DEnc GstMpegH3DEnc;
typedef struct _GstMpegH3DEncClass GstMpegH3DEncClass;
typedef struct _Mpegh3daencState Mpegh3daencState;

struct _GstMpegH3DEnc
{
  GstAudioEncoder parent;

  Mpegh3daencState *state;
};

struct _GstMpegH3DEncClass
{
  GstAudioEncoderClass parent_class;
};

GType gst_mpegh3daenc_get_type (void);

G_END_DECLS

#endif /* __GST_MPEGH3DAENC_H__ */
