/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2014> Jurgen Slowack <jurgenslowack@gmail.com>
 * Copyright (C) <2021> Intel Corporation
 * Copyright (C) <2024> César Fabián Orccón Chipana <cfoch.fabian@gmail.com>
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

#ifndef __GST_RTP_H266_DEPAY_H__
#define __GST_RTP_H266_DEPAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasedepayload.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

typedef enum
{
  GST_H266_ALIGNMENT_AU,
  GST_H266_ALIGNMENT_NAL,
  GST_H266_ALIGNMENT_UNKOWN,
} GstH266Alignment;

typedef enum
{
  GST_H266_STREAM_FORMAT_UNKNOWN,
  GST_H266_STREAM_FORMAT_BYTESTREAM,
  GST_H266_STREAM_FORMAT_HVC1,
  GST_H266_STREAM_FORMAT_HEV1
} GstH266StreamFormat;

/* Imported from gsth266parse */
/* *INDENT-OFF* */
typedef enum
{
  GST_H266_NAL_SLICE_TRAIL      = 0,
  GST_H266_NAL_SLICE_STSA       = 1,
  GST_H266_NAL_SLICE_RADL       = 2,
  GST_H266_NAL_SLICE_RASL       = 3,
  GST_H266_NAL_SLICE_IDR_W_RADL = 7,
  GST_H266_NAL_SLICE_IDR_N_LP   = 8,
  GST_H266_NAL_SLICE_CRA        = 9,
  GST_H266_NAL_SLICE_GDR        = 10,
  GST_H266_NAL_OPI              = 12,
  GST_H266_NAL_DCI              = 13,
  GST_H266_NAL_VPS              = 14,
  GST_H266_NAL_SPS              = 15,
  GST_H266_NAL_PPS              = 16,
  GST_H266_NAL_PREFIX_APS       = 17,
  GST_H266_NAL_SUFFIX_APS       = 18,
  GST_H266_NAL_PH               = 19,
  GST_H266_NAL_AUD              = 20,
  GST_H266_NAL_EOS              = 21,
  GST_H266_NAL_EOB              = 22,
  GST_H266_NAL_PREFIX_SEI       = 23,
  GST_H266_NAL_SUFFIX_SEI       = 24,
  GST_H266_NAL_FD               = 25,
  GST_H266_NAL_AP               = 28,
  GST_H266_NAL_FU               = 29,
} GstH266NalUnitType;
/* *INDENT-ON* */

#define GST_TYPE_RTP_H266_DEPAY gst_rtp_h266_depay_get_type()
G_DECLARE_FINAL_TYPE(GstRtpH266Depay, gst_rtp_h266_depay, GST, RTP_H266_DEPAY,
                     GstRTPBaseDepayload);

struct _GstRtpH266Depay
{
  GstRTPBaseDepayload depayload;

  GstAdapter *adapter;

  /* nal merging */
  GstH266Alignment alignment;
  GstAdapter *picture_adapter;
  GstClockTime last_ts;
  gboolean last_keyframe;

  /* FU */
  guint8 current_fu_type;
  guint16 last_fu_seqnum;
  GstClockTime fu_timestamp;
  gboolean fu_marker;
};

G_END_DECLS
#endif /* __GST_RTP_H266_DEPAY_H__ */
