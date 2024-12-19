/* GStreamer
 * Copyright (C) <2024> Carlos Falgueras García <cfalgueras@fluendo.com>
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

#ifndef __GST_RTP_H266_PAY_H__
#define __GST_RTP_H266_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H266_PAY gst_rtp_h266_pay_get_type()
G_DECLARE_FINAL_TYPE(GstRtpH266Pay, gst_rtp_h266_pay, GST, RTP_H266_PAY,
                     GstRTPBasePayload);

G_END_DECLS
#endif /* __GST_RTP_H266_PAY_H__ */
