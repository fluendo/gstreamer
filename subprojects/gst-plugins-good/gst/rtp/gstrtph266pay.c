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

/* references:
 * - RTP Payload Format for Versatile Video Coding (VVC)
 *   https://www.ietf.org/rfc/rfc9328.txt
 * - RTP: A Transport Protocol for Real-Time Applications
 *   https://www.ietf.org/rfc/rfc3550.txt
 * - H.266: Versatile video coding
 *   https://www.itu.int/rec/T-REC-H.266-202309-I
 */

#include "gstrtph266pay.h"
#include <gst/rtp/gstrtppayloads.h>
#include <gst/rtp/gstrtpelements.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtputils.h>
#include <gst/base/gstadapter.h>

#define GST_CAT_DEFAULT rtph266pay_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef enum
{
  NALU_TYPE_VPS = 14,
  NALU_TYPE_SPS = 15,
  NALU_TYPE_PPS = 16,
  NALU_TYPE_PAPS = 17,
  NALU_TYPE_SAPS = 18,
  NALU_TYPE_AUD = 20,
} NaluType;

#define NALU_SC_MSK 0xffffff00
#define NALU_SC_VAL 0x00000100
#define NALU_SC_LEN 3
#define NALU_HDR_LEN 2
#define NALU_INVALID_XPS 0xFF
#define NALU_IS_PARAMETER_SET(nalu) \
  (((nalu)->type >= NALU_TYPE_VPS) && ((nalu)->type <= NALU_TYPE_SAPS))

#define FU_TYPE 29
#define FU_HDR_LEN (NALU_HDR_LEN + 1)   // PayloadHdr + FU header

typedef struct
{
  GstBuffer *nalu_buf;
  GstBuffer *hdr_buf;
  GstBuffer *rbsp_buf;
  guint16 size;                 // Size of the NALU (inluding its header)
  NaluType type;                // nal_unit_type
  guint8 xps_id;                // {vps_video,sps_seq,pps_pic,aps_adaptation}_parameter_set_id
  gboolean au_start;
  gboolean au_end;
} Nalu;

#define NALU_PTR_FORMAT \
  "p, size: %u, type: %u, xps_id: %u, au_start: %s, au_end: %s"
#define NALU_ARGS(nalu) \
  (nalu), (nalu)->size, (nalu)->type, (nalu)->xps_id, (nalu)->au_start ? "true" : "false", (nalu)->au_end ? "true" : "false"

typedef enum
{
  ALIGNMENT_AU,
  ALIGNMENT_NAL,
  ALIGNMENT_UNKOWN,
} Alignment;

struct _GstRtpH266Pay
{
  GstRTPBasePayload payload;

  GstAdapter *adapter;
  GQueue nalus;
  Alignment alignment;
};

#define gst_rtp_h266_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpH266Pay, gst_rtp_h266_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtph266pay, "rtph266pay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_H266_PAY, rtp_element_init (plugin));

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h266, stream-format = (string) byte-stream, "
        "alignment = (string) { nal, au }"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H266\"")
    );

enum
{
  PROP_0,
};

// GObject methods
static void _finalize (GObject * object);
static void _set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void _get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec);

// GstElement methods
static GstStateChangeReturn _change_state (GstElement * element,
    GstStateChange transition);

// GstRTPBasePayload methods
static gboolean _set_caps (GstRTPBasePayload * rtpbasepay, GstCaps * caps);
static GstFlowReturn _handle_buffer (GstRTPBasePayload * rtpbasepay,
    GstBuffer * buffer);
static gboolean _sink_event (GstRTPBasePayload * rtpbasepay, GstEvent * event);

// GstRtpH266Pay methods
static void _process_nalu (GstRtpH266Pay * rtph266pay, GstBuffer * nalu_buf);
static void _set_au_boundaries (GstRtpH266Pay * rtph266pay);
static GstFlowReturn _push_pending_data (GstRtpH266Pay * rtph266pay,
    gboolean eos);
static GstFlowReturn _push_up (GstRtpH266Pay * rtph266pay, const Nalu * nalu);
static GstFlowReturn _push_fu (GstRtpH266Pay * rtph266pay, const Nalu * nalu);
//static GstFlowReturn _push_ap (GstRtpH266Pay * rtph266pay, GQueue *nalus);
static gboolean _up_fits_in_mtu (const GstRtpH266Pay * rtph266pay,
    const Nalu * nalu);
static GstBuffer *_extract_fu (GstRtpH266Pay * rtph266pay, const Nalu * nalu,
    gsize offset, gsize size, gboolean last);
static void _clear_nalu_queue (GstRtpH266Pay * rtph266pay);

// Nalu methods
static Nalu *_nalu_new (GstBuffer * nalu_buf);
static void _nalu_free (Nalu * nalu);

static void
gst_rtp_h266_pay_class_init (GstRtpH266PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (_get_property);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (_change_state);

  gstrtpbasepayload_class->set_caps = GST_DEBUG_FUNCPTR (_set_caps);
  gstrtpbasepayload_class->handle_buffer = GST_DEBUG_FUNCPTR (_handle_buffer);
  gstrtpbasepayload_class->sink_event = GST_DEBUG_FUNCPTR (_sink_event);

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "RTP H266 payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode H266 video into RTP packets (RFC 9328)",
      "Carlos Falgueras García <cfalgueras@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "rtph266pay", 0,
      "H266 RTP Payloader");
}

static void
gst_rtp_h266_pay_init (GstRtpH266Pay * rtph266pay)
{
  rtph266pay->adapter = gst_adapter_new ();
  g_queue_init (&rtph266pay->nalus);
  rtph266pay->alignment = ALIGNMENT_UNKOWN;
}

static void
_finalize (GObject * object)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (object);

  g_clear_pointer (&rtph266pay->adapter, g_object_unref);
  _clear_nalu_queue (rtph266pay);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
}

static void
_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
}

static GstStateChangeReturn
_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (element);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
    gst_adapter_clear (rtph266pay->adapter);
    _clear_nalu_queue (rtph266pay);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static gboolean
_set_caps (GstRTPBasePayload * rtpbasepay, GstCaps * caps)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (rtpbasepay);
  const gchar *alignment_str;
  GstStructure *s;

  gst_rtp_base_payload_set_options (rtpbasepay, "video", TRUE, "H266", 90000);

  s = gst_caps_get_structure (caps, 0);
  g_assert (s);
  alignment_str = gst_structure_get_string (s, "alignment");
  if (alignment_str) {
    if (g_str_equal (alignment_str, "au")) {
      rtph266pay->alignment = ALIGNMENT_AU;
    } else if (g_str_equal (alignment_str, "nal")) {
      rtph266pay->alignment = ALIGNMENT_NAL;
    } else {
      rtph266pay->alignment = ALIGNMENT_UNKOWN;
    }
  }

  return TRUE;
}

static GstFlowReturn
_handle_buffer (GstRTPBasePayload * rtpbasepay, GstBuffer * buffer)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (rtpbasepay);
  GstAdapter *adapter = rtph266pay->adapter;
  gsize adapter_size;

  GST_DEBUG_OBJECT (rtph266pay, "New buffer %" GST_PTR_FORMAT, buffer);
  gst_adapter_push (adapter, buffer);

  // Advance until the first start code to skip heading zeroes
  adapter_size = gst_adapter_available (adapter);
  gssize next_nalu_start =
      gst_adapter_masked_scan_uint32 (adapter, NALU_SC_MSK, NALU_SC_VAL, 0,
      adapter_size);
  if (next_nalu_start < 0) {
    GST_WARNING_OBJECT (rtph266pay, "Not NALU found");
    gst_adapter_flush (adapter, adapter_size);
    return GST_FLOW_OK;
  }
  gst_adapter_flush (adapter, next_nalu_start);

  // For each NALU
  while ((adapter_size = gst_adapter_available (adapter)) > NALU_SC_LEN) {
    // Find next start code, skipping the actual one
    gssize next_nalu_start =
        gst_adapter_masked_scan_uint32 (adapter, NALU_SC_MSK, NALU_SC_VAL,
        NALU_SC_LEN, adapter_size - NALU_SC_LEN);

    // If no start code found, the length of the NALU is the remaining size
    gssize nalu_len = (next_nalu_start < 0) ? adapter_size : next_nalu_start;

    if (nalu_len <= NALU_SC_LEN) {
      GST_WARNING_OBJECT (rtph266pay, "NALU too small %ld, skipping it",
          nalu_len);
      gst_adapter_flush (adapter, nalu_len);
      continue;
    }

    // Remove the start code
    nalu_len -= NALU_SC_LEN;    // Here, it'll always be > 0
    gst_adapter_flush (adapter, NALU_SC_LEN);

    GstBuffer *nalu_buf = gst_adapter_take_buffer (adapter, nalu_len);
    // nalu_buf will never be aligned with an incoming buffer, therefore
    // gst_adapter_take_buffer() will never set its timestamps.
    GST_BUFFER_PTS (nalu_buf) = gst_adapter_prev_pts (adapter, NULL);
    GST_BUFFER_DTS (nalu_buf) = gst_adapter_prev_dts (adapter, NULL);
    _process_nalu (rtph266pay, nalu_buf);
  }

  return _push_pending_data (rtph266pay, FALSE);
}

static gboolean
_sink_event (GstRTPBasePayload * rtpbasepay, GstEvent * event)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (rtpbasepay);
  GstFlowReturn ret = GST_FLOW_OK;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (rtph266pay->adapter);
      _clear_nalu_queue (rtph266pay);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (rtph266pay, "EOS: Draining");
      ret = _push_pending_data (rtph266pay, TRUE);
      break;
    default:
      break;
  }

  if (ret != GST_FLOW_OK)
    return FALSE;

  return GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->sink_event (rtpbasepay,
      event);
}

static void
_process_nalu (GstRtpH266Pay * rtph266pay, GstBuffer * nalu_buf)
{
  Nalu *nalu;

  g_assert (nalu_buf);

  // Parse NALU
  nalu = _nalu_new (nalu_buf);
  if (!nalu) {
    GST_WARNING_OBJECT (rtph266pay,
        "Couldn't decode NALU %" GST_PTR_FORMAT ". Dropping it.", nalu_buf);
    return;
  }
  GST_DEBUG_OBJECT (rtph266pay, "NALU decoded: %" NALU_PTR_FORMAT,
      NALU_ARGS (nalu));

  g_queue_push_tail (&rtph266pay->nalus, nalu);
}

static void
_set_au_boundaries (GstRtpH266Pay * rtph266pay)
{
  GList *head = g_queue_peek_head_link (&rtph266pay->nalus);
  Nalu *last_nalu = g_queue_peek_tail (&rtph266pay->nalus);

  for (GList * l = head; l != NULL; l = l->next) {
    Nalu *nalu = l->data;
    Nalu *prev_nalu = l->prev ? l->prev->data : NULL;
    // TODO gboolean discont = GST_BUFFER_IS_DISCONT (nalu->nalu_buf);
    GstClockTime prev_pts = GST_CLOCK_TIME_NONE;
    GstClockTime prev_dts = GST_CLOCK_TIME_NONE;
    GstClockTime pts = GST_BUFFER_PTS (nalu->nalu_buf);
    GstClockTime dts = GST_BUFFER_DTS (nalu->nalu_buf);
    if (prev_nalu) {
      prev_pts = GST_BUFFER_PTS (prev_nalu->nalu_buf);
      prev_dts = GST_BUFFER_DTS (prev_nalu->nalu_buf);
    }
    gboolean aud = nalu->type == NALU_TYPE_AUD;
    gboolean new_ts = (prev_pts != pts) || (prev_dts != dts);

    nalu->au_start = aud || new_ts /*|| discont */ ;
    if (prev_nalu && nalu->au_start) {
      prev_nalu->au_end = TRUE;
      GST_DEBUG_OBJECT (rtph266pay, "AU start found -> previous AU finished");
    }
  }

  // In some cases, we already know where is the end of an AU. Mark it now to
  // avoid waiting for the next NALU.
  if (last_nalu) {
    gboolean au_alignment = rtph266pay->alignment == ALIGNMENT_AU;
    gboolean marker =
        GST_BUFFER_FLAG_IS_SET (last_nalu->nalu_buf, GST_BUFFER_FLAG_MARKER);
    last_nalu->au_end = last_nalu->au_end || au_alignment || marker;
  }
}

static GstFlowReturn
_push_pending_data (GstRtpH266Pay * rtph266pay, gboolean eos)
{
  GQueue *nalus = &rtph266pay->nalus;
  GstFlowReturn ret = GST_FLOW_OK;
  Nalu *nalu;

  // TODO: handle AP
  // TODO: send XPS if needed

  // TODO: Maybe it's worth to avoid iterating the NALU list twice, maybe not
  _set_au_boundaries (rtph266pay);

  // Try to push all NALUs
  while ((nalu = g_queue_pop_head (nalus))) {
    gboolean is_last = g_queue_get_length (nalus) == 0;

    // Can't push the last NALU without knowing if it's the end of an AU,
    // because setting the M bit could be necessary. But have to push it if
    // we're on EOS
    if (!eos && is_last && !nalu->au_end) {
      GST_DEBUG_OBJECT (rtph266pay, "Keeping last NALU: %" NALU_PTR_FORMAT,
          NALU_ARGS (nalu));
      g_queue_push_head (nalus, nalu);
      break;
    }

    GST_DEBUG_OBJECT (rtph266pay, "Pushing NALU: %" NALU_PTR_FORMAT,
        NALU_ARGS (nalu));

    if (_up_fits_in_mtu (rtph266pay, nalu))
      ret = _push_up (rtph266pay, nalu);
    else
      ret = _push_fu (rtph266pay, nalu);

    _nalu_free (nalu);
    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;
}

static GstFlowReturn
_push_up (GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  GstRTPBasePayload *rtpbasepay = GST_RTP_BASE_PAYLOAD (rtph266pay);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *out_buf;

  // Allocate just the RTP header. We'll add the buffer payload later. This way
  // we avoid unnecessary copies
  out_buf = gst_rtp_base_payload_allocate_output_buffer (rtpbasepay, 0, 0, 0);
  if (!gst_rtp_buffer_map (out_buf, GST_MAP_WRITE, &rtp))
    goto error;

  // Copy buffer metadata
  gst_buffer_copy_into (out_buf, nalu->nalu_buf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  gst_rtp_buffer_set_marker (&rtp, nalu->au_end);

  // Append the payload to the output buffer
  out_buf = gst_buffer_append (out_buf, gst_buffer_ref (nalu->hdr_buf));
  // TODO: Conditionally append DONL
  out_buf = gst_buffer_append (out_buf, gst_buffer_ref (nalu->rbsp_buf));
  g_assert (out_buf);

  gst_rtp_buffer_unmap (&rtp);
  GST_DEBUG_OBJECT (rtph266pay, "Pushing UP %" GST_PTR_FORMAT, out_buf);
  return gst_rtp_base_payload_push (rtpbasepay, out_buf);
error:
  gst_buffer_unref (out_buf);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
_push_fu (GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  GstRTPBasePayload *rtpbasepay = GST_RTP_BASE_PAYLOAD (rtph266pay);
  GstBufferList *out_buflist = gst_buffer_list_new ();

  // TODO: Consider DONL
  guint mtu = GST_RTP_BASE_PAYLOAD_MTU (rtpbasepay);
  guint rtp_pyl_max_size = gst_rtp_buffer_calc_payload_len (mtu, 0, 0);
  guint fu_pyl_max_size = rtp_pyl_max_size - FU_HDR_LEN;
  gsize rbsp_size = nalu->size - NALU_HDR_LEN;
  g_assert_cmpuint (fu_pyl_max_size, <, rbsp_size);

  // Split the NALU into several FU
  for (gsize offset = 0; offset < rbsp_size; offset += fu_pyl_max_size) {
    gsize remaining = rbsp_size - offset;
    gboolean last = remaining <= fu_pyl_max_size;
    gsize size = last ? remaining : fu_pyl_max_size;

    GstBuffer *fu_buf = _extract_fu (rtph266pay, nalu, offset, size, last);
    GST_DEBUG_OBJECT (rtph266pay, "FU [%lu, %lu] %" GST_PTR_FORMAT,
        offset, offset + size, fu_buf);
    gst_buffer_list_add (out_buflist, fu_buf);
  }

  GST_DEBUG_OBJECT (rtph266pay, "Pushing FU GstBufferList %" GST_PTR_FORMAT,
      out_buflist);
  return gst_rtp_base_payload_push_list (rtpbasepay, out_buflist);
}

static gboolean
_up_fits_in_mtu (const GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  guint mtu = GST_RTP_BASE_PAYLOAD_MTU (rtph266pay);
  guint payload_size = nalu->size;      // TODO: Consider DONL
  gboolean fits = gst_rtp_buffer_calc_packet_len (payload_size, 0, 0) <= mtu;

  if (!fits) {
    GST_DEBUG_OBJECT (rtph266pay,
        "NALU does not fit into %u MTU: %" GST_PTR_FORMAT, mtu, nalu->nalu_buf);
  }
  return fits;
}

static GstBuffer *
_extract_fu (GstRtpH266Pay * rtph266pay, const Nalu * nalu,
    gsize offset, gsize size, gboolean last)
{
  GstRTPBasePayload *rtpbasepay = GST_RTP_BASE_PAYLOAD (rtph266pay);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean au_end = last && nalu->au_end;
  gboolean first = (offset == 0);
  GstBuffer *fu_pyl_buf;
  GstBuffer *fu_buf;
  guint8 *fu_hdr;

  fu_pyl_buf = gst_buffer_copy_region (nalu->rbsp_buf, GST_BUFFER_COPY_MEMORY,
      offset, size);
  g_assert (fu_pyl_buf);

  // Allocate just the RTP header + PayloadHdr + FU header. We'll add the buffer
  // payload later. This way we avoid unnecessary copies
  fu_buf =
      gst_rtp_base_payload_allocate_output_buffer (rtpbasepay, FU_HDR_LEN, 0,
      0);
  gboolean ok = gst_rtp_buffer_map (fu_buf, GST_MAP_WRITE, &rtp);
  g_assert (ok);

  // Copy required buffer metadata
  gst_buffer_copy_into (fu_buf, nalu->nalu_buf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  gst_rtp_buffer_set_marker (&rtp, au_end);

  fu_hdr = gst_rtp_buffer_get_payload (&rtp);
  g_assert (fu_hdr);

  // Setup PayloadHdr and FU Header
  // |     PayloadHdr (NALU HDR)     |   FU HEADER   |
  // |-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-|
  // |F|Z| LayerID   | Type(29)| TID |S|E|P|  FuType |
  // +---------------+---------------|---------------+
  guint8 read = gst_buffer_extract (nalu->hdr_buf, 0, fu_hdr, NALU_HDR_LEN);
  g_assert_cmpuint (read, >=, NALU_HDR_LEN);
  fu_hdr[1] = (FU_TYPE << 3) | (fu_hdr[1] & 0x07);      // set Type = 29
  fu_hdr[2] = (!!first << 7) | (!!last << 6) | (!!au_end << 5) | nalu->type;

  gst_rtp_buffer_unmap (&rtp);

  // TODO: Conditionally append DONL
  fu_buf = gst_buffer_append (fu_buf, fu_pyl_buf);
  g_assert (fu_buf);

  return fu_buf;
}

static void
_clear_nalu_queue (GstRtpH266Pay * rtph266pay)
{
  g_queue_clear_full (&rtph266pay->nalus, (GDestroyNotify) _nalu_free);
}

static Nalu *
_nalu_new (GstBuffer * nalu_buf)
{
  // |            NALU HDR           |  2 first bytes of XPS
  // |-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-:-+-+-+-+-+-+-+-:-+-+
  // |F|Z| LayerID   |   Type  | TID | [VPS, PPS, SPS, PAPS, SAPS]   : ...
  // +---------------+---------------|---------------:---------------:----
  //                        VPS/SPS: |X X X X . . . .:. . . . . . . .:
  //                            PPS: |X X X X X X . .:. . . . . . . .:
  //                      PAPS/SAPS: |. . . . . . X X:X X . . . . . .:
  guint8 data[NALU_HDR_LEN + 2];
  Nalu *nalu;

  if (gst_buffer_extract (nalu_buf, 0, data, sizeof (data)) < sizeof (data)) {
    gst_buffer_unref (nalu_buf);
    return NULL;
  }

  nalu = g_new (Nalu, 1);
  nalu->size = gst_buffer_get_size (nalu_buf);
  nalu->type = data[1] >> 3;
  switch (nalu->type) {
    case NALU_TYPE_VPS:
    case NALU_TYPE_SPS:
      nalu->xps_id = data[2] >> 4;
      break;
    case NALU_TYPE_PPS:
      nalu->xps_id = data[2] >> 2;
      break;
    case NALU_TYPE_PAPS:
    case NALU_TYPE_SAPS:
      nalu->xps_id = ((data[2] & 0x03) << 2) | ((data[3] & 0xC0) >> 6);
      break;
    default:
      nalu->xps_id = NALU_INVALID_XPS;
  }

  nalu->nalu_buf = nalu_buf;
  nalu->hdr_buf =
      gst_buffer_copy_region (nalu_buf, GST_BUFFER_COPY_MEMORY, 0,
      NALU_HDR_LEN);
  nalu->rbsp_buf =
      gst_buffer_copy_region (nalu_buf, GST_BUFFER_COPY_MEMORY, NALU_HDR_LEN,
      -1);

  nalu->au_start = FALSE;
  nalu->au_end = FALSE;

  return nalu;
}

static void
_nalu_free (Nalu * nalu)
{
  gst_buffer_unref (nalu->nalu_buf);
  gst_buffer_unref (nalu->hdr_buf);
  gst_buffer_unref (nalu->rbsp_buf);
  g_free (nalu);
}
