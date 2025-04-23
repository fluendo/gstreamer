/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
 * Copyright (C) 2020 Safran Passenger Innovations LLC
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

#pragma once

#define GST_USE_UNSTABLE_API
#include <gst/codecs/gsth265decoder.h>

#include "gstv4l2decoder.h"

G_BEGIN_DECLS

#define GST_TYPE_V4L2_CODEC_H265_DEC           (gst_v4l2_codec_h265_dec_get_type())
#define GST_V4L2_CODEC_H265_DEC(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_CODEC_H265_DEC,GstV4l2CodecH265Dec))
#define GST_V4L2_CODEC_H265_DEC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_CODEC_H265_DEC,GstV4l2CodecH265DecClass))
#define GST_V4L2_CODEC_H265_DEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_V4L2_CODEC_H265_DEC, GstV4l2CodecH265DecClass))
#define GST_IS_V4L2_CODEC_H265_DEC(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_CODEC_H265_DEC))
#define GST_IS_V4L2_CODEC_H265_DEC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_CODEC_H265_DEC))

typedef struct _GstV4l2CodecH265Dec GstV4l2CodecH265Dec;
typedef struct _GstV4l2CodecH265DecClass GstV4l2CodecH265DecClass;

struct _GstV4l2CodecH265DecClass
{
  GstH265DecoderClass parent_class;
  GstV4l2CodecDevice *device;
};

GType gst_v4l2_codec_h265_dec_get_type (void);
void  gst_v4l2_codec_h265_dec_register (GstPlugin * plugin,
                                        GstV4l2Decoder * decoder,
                                        GstV4l2CodecDevice * device,
                                        guint rank);

G_END_DECLS
