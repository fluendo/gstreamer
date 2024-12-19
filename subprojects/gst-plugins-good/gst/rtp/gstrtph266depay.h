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
#include <gst/rtp/gstrtph266common.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

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
