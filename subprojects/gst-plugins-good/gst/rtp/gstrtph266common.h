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

#ifndef __GST_RTP_H266_COMMON_H__
#define __GST_RTP_H266_COMMON_H__

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

/* *INDENT-OFF* */
typedef enum
{
  GST_H266_NAL_SLICE_TRAIL      = 0,
  GST_H266_NAL_SLICE_STSA       = 1,
  GST_H266_NAL_SLICE_RADL       = 2,
  GST_H266_NAL_SLICE_RASL       = 3,
  GST_H266_NAL_RSV_VCL_4        = 4,
  GST_H266_NAL_RSV_VCL_5        = 5,
  GST_H266_NAL_RSV_VCL_6        = 6,
  GST_H266_NAL_SLICE_IDR_W_RADL = 7,
  GST_H266_NAL_SLICE_IDR_N_LP   = 8,
  GST_H266_NAL_SLICE_CRA        = 9,
  GST_H266_NAL_SLICE_GDR        = 10,
  GST_H266_NAL_RSV_IRAP_11      = 11,
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
  GST_H266_NAL_RSV_NVCL_26      = 26,
  GST_H266_NAL_RSV_NVCL_27      = 27,
  GST_H266_NAL_AP               = 28,
  GST_H266_NAL_FU               = 29,
} GstH266NalUnitType;
/* *INDENT-ON* */

#endif
