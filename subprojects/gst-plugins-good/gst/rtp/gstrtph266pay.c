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
  NALU_TYPE_TRAIL_NUT = 0,
  NALU_TYPE_STSA_NUT = 1,
  NALU_TYPE_RADL_NUT = 2,
  NALU_TYPE_RASL_NUT = 3,
  NALU_TYPE_RSV_VCL_4 = 4,
  NALU_TYPE_RSV_VCL_5 = 5,
  NALU_TYPE_RSV_VCL_6 = 6,
  NALU_TYPE_IDR_W_RADL = 7,
  NALU_TYPE_IDR_N_LP = 8,
  NALU_TYPE_CRA_NUT = 9,
  NALU_TYPE_GDR_NUT = 10,
  NALU_TYPE_RSV_IRAP_11 = 11,
  NALU_TYPE_OPI_NUT = 12,
  NALU_TYPE_DCI_NUT = 13,
  NALU_TYPE_VPS_NUT = 14,
  NALU_TYPE_SPS_NUT = 15,
  NALU_TYPE_PPS_NUT = 16,
  NALU_TYPE_PREFIX_APS_NUT = 17,
  NALU_TYPE_SUFFIX_APS_NUT = 18,
  NALU_TYPE_PH_NUT = 19,
  NALU_TYPE_AUD_NUT = 20,
  NALU_TYPE_EOS_NUT = 21,
  NALU_TYPE_EOB_NUT = 22,
  NALU_TYPE_PREFIX_SEI_NUT = 23,
  NALU_TYPE_SUFFIX_SEI_NUT = 24,
  NALU_TYPE_FD_NUT = 25,
  NALU_TYPE_RSV_NVCL_26 = 26,
  NALU_TYPE_RSV_NVCL_27 = 27,
  _NALU_TYPE_MAX
} NaluType;

#define NALU_SC_MSK 0xffffff00
#define NALU_SC_VAL 0x00000100
#define NALU_SC_LEN 3
#define NALU_HDR_LEN 2
#define NALU_INVALID_PS 0xFF
#define NALU_IS_PS(nalu) \
  (((nalu)->type >= NALU_TYPE_VPS_NUT) && ((nalu)->type <= NALU_TYPE_SUFFIX_APS_NUT))
#define NALU_IS_VCL(nalu) \
  (((nalu)->type >= NALU_TYPE_TRAIL_NUT) && ((nalu)->type <= NALU_TYPE_RSV_IRAP_11))
#define NALU_IS_IDR(nalu) \
  (((nalu)->type == NALU_TYPE_IDR_W_RADL) || ((nalu)->type == NALU_TYPE_IDR_N_LP))

#define AP_TYPE 28
#define FU_TYPE 29
#define FU_HDR_LEN (NALU_HDR_LEN + 1)   // PayloadHdr + FU header

typedef struct
{
  GstBuffer *nalu_buf;
  GstBuffer *hdr_buf;
  GstBuffer *rbsp_buf;
  guint16 size;                 // Size of the NALU (inluding its header)
  gboolean f_bit;               // forbidden_zero_bit
  guint8 layer_id;              // nuh_layer_id
  NaluType type;                // nal_unit_type
  guint8 ps_id;                 // {vps_video,sps_seq,pps_pic,aps_adaptation}_parameter_set_id
  gboolean au_start;
  gboolean au_end;
} Nalu;

#define NALU_PTR_FORMAT \
  "p, size: %u, f_bit: %d, layer_id: %d, type: %u, ps_id: %u, au_start: %s, au_end: %s"
#define NALU_ARGS(nalu) \
  (nalu), (nalu)->size, (nalu)->f_bit, (nalu)->layer_id, (nalu)->type, (nalu)->ps_id, (nalu)->au_start ? "true" : "false", (nalu)->au_end ? "true" : "false"

typedef enum
{
  ALIGNMENT_AU,
  ALIGNMENT_NAL,
  ALIGNMENT_UNKNOWN,
} Alignment;

typedef enum
{
  AGGREGATE_NONE,
  AGGREGATE_ZERO_LATENCY,
  AGGREGATE_MAX,
} AggregateMode;
#define TYPE_AGGREGATE_MODE _aggregate_mode_get_type ()

typedef struct
{
  GQueue nalus;
  gsize nalu_size_sum;
  guint min_layer_id;
  gboolean f_bit;
} AggregatedNalus;

struct _GstRtpH266Pay
{
  GstRTPBasePayload payload;

  GstAdapter *adapter;
  GQueue nalus;
  AggregatedNalus aggregated_nalus;
  GHashTable *ps_id_nalu_map;

  Alignment alignment;
  GstClockTime ts_last_ps_to_sent;
  gint fps_n;
  gint fps_d;

  // Properties
  gint config_interval;
  AggregateMode aggregate_mode;
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

#define DEFAULT_CONFIG_INTERVAL 0
#define DEFAULT_AGGREGATE_MODE AGGREGATE_NONE

enum
{
  PROP_0,
  PROP_CONFIG_INTERVAL,
  PROP_AGGREGATE_MODE,
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
static void _update_ps_cache (GstRtpH266Pay * rtph266pay, const Nalu * nalu);
static void _clear_ps_cache (GstRtpH266Pay * rtph266pay);
static gboolean _can_insert_ps (GstRtpH266Pay * rtph266pay, const Nalu * nalu);
static void _insert_ps_cache (GstRtpH266Pay * rtph266pay);
static void _set_au_boundaries (GstRtpH266Pay * rtph266pay);
static GstFlowReturn _push_pending_data (GstRtpH266Pay * rtph266pay,
    gboolean eos);
static GstFlowReturn _push_unit_pkt (GstRtpH266Pay * rtph266pay, Nalu * nalu);
static GstFlowReturn _push_fragmented (GstRtpH266Pay * rtph266pay, Nalu * nalu);
static GstFlowReturn _push_aggregated (GstRtpH266Pay * rtph266pay);
static gboolean _up_fits_in_mtu (const GstRtpH266Pay * rtph266pay,
    const Nalu * nalu);
static gboolean _ap_fits_in_mtu (const GstRtpH266Pay * rtph266pay,
    const Nalu * nalu);
static gboolean _can_aggregate_nalu (GstRtpH266Pay * rtph266pay, Nalu * nalu);
static GstBuffer *_extract_fu (GstRtpH266Pay * rtph266pay, const Nalu * nalu,
    gsize offset, gsize size, gboolean last);
static void _clear_nalu_queues (GstRtpH266Pay * rtph266pay);
static GstClockTime _get_nalu_running_time (const GstRtpH266Pay * rtph266pay,
    const Nalu * nalu);

// Nalu methods
static Nalu *_nalu_new (GstBuffer * nalu_buf);
static void _nalu_free (Nalu * nalu);
static Nalu *_nalu_copy (const Nalu * nalu);
static Nalu *_nalu_copy_ts (Nalu * dst, const Nalu * src);
static gboolean _nalu_is_rsv (const Nalu * nalu);

// AggregatedNalus methods
static void _aggregated_nalus_init (AggregatedNalus * aggregated_nalus);
static void _aggregated_nalus_clear (AggregatedNalus * aggregated_nalus);
static void _aggregated_nalus_add (AggregatedNalus * aggregated_nalus,
    Nalu * nalu);
static gboolean _aggregated_nalus_is_empty (AggregatedNalus * aggregated_nalus);
static gboolean _aggregated_nalus_have_au (AggregatedNalus * aggregated_nalus);

// Others
static GType _aggregate_mode_get_type (void);
static gboolean _src_query (GstPad * pad, GstObject * parent, GstQuery * query);

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

  g_object_class_install_property (gobject_class,
      PROP_CONFIG_INTERVAL,
      g_param_spec_int ("config-interval",
          "Parameter Set send interval",
          "Send VPS, SPS, PPS and APS at this interval (in seconds)"
          "(0 = disabled, -1 = send with every IDR frame)",
          -1, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_AGGREGATE_MODE,
      g_param_spec_enum ("aggregate-mode",
          "Attempt to use aggregate packets",
          "Bundle suitable Parameter Set NAL units into aggregate packets.",
          TYPE_AGGREGATE_MODE,
          DEFAULT_AGGREGATE_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

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
  _aggregated_nalus_init (&rtph266pay->aggregated_nalus);
  rtph266pay->ps_id_nalu_map =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) _nalu_free);
  rtph266pay->alignment = ALIGNMENT_UNKNOWN;
  rtph266pay->ts_last_ps_to_sent = GST_CLOCK_TIME_NONE;
  rtph266pay->config_interval = DEFAULT_CONFIG_INTERVAL;
  rtph266pay->aggregate_mode = DEFAULT_AGGREGATE_MODE;

  gst_pad_set_query_function (GST_RTP_BASE_PAYLOAD_SRCPAD (rtph266pay),
      _src_query);
}

static void
_finalize (GObject * object)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (object);

  g_clear_pointer (&rtph266pay->adapter, g_object_unref);
  g_clear_pointer (&rtph266pay->ps_id_nalu_map, g_hash_table_destroy);
  _clear_nalu_queues (rtph266pay);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (object);

  GST_OBJECT_LOCK (rtph266pay);
  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      rtph266pay->config_interval = g_value_get_int (value);
      break;
    case PROP_AGGREGATE_MODE:
      rtph266pay->aggregate_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (rtph266pay);
}

static void
_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (object);

  GST_OBJECT_LOCK (rtph266pay);
  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      g_value_set_int (value, rtph266pay->config_interval);
      break;
    case PROP_AGGREGATE_MODE:
      g_value_set_enum (value, rtph266pay->aggregate_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (rtph266pay);
}

static GstStateChangeReturn
_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (element);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
    gst_adapter_clear (rtph266pay->adapter);
    _clear_nalu_queues (rtph266pay);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  // Chain up to the parent class first to avoid a race condition
  // Check commit df724c410b02b82bd7db893d24e8572a06c2fcb1
  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    _clear_ps_cache (rtph266pay);

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
      rtph266pay->alignment = ALIGNMENT_UNKNOWN;
    }
  }

  gint fps_n = 0;
  gint fps_d = 0;
  gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);
  rtph266pay->fps_n = fps_n;
  rtph266pay->fps_d = fps_d;

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
      _clear_nalu_queues (rtph266pay);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (rtph266pay, "EOS: Draining");
      ret = _push_pending_data (rtph266pay, TRUE);
      break;
    case GST_EVENT_STREAM_START:
      _clear_ps_cache (rtph266pay);
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

  _update_ps_cache (rtph266pay, nalu);
  g_queue_push_tail (&rtph266pay->nalus, nalu);
}

static void
_update_ps_cache (GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  GHashTable *map = rtph266pay->ps_id_nalu_map;
  guint8 type = nalu->type;
  guint8 id = nalu->ps_id;
  Nalu *new_nalu;
  gpointer *key;

  if (!NALU_IS_PS (nalu))
    return;

  new_nalu = _nalu_copy (nalu);
  new_nalu->au_start = FALSE;
  new_nalu->au_end = FALSE;
  key = GINT_TO_POINTER ((type << 8) | id);
  if (g_hash_table_insert (map, key, new_nalu))
    GST_DEBUG_OBJECT (rtph266pay, "PS(%u,%u) cached", type, id);
  else
    GST_DEBUG_OBJECT (rtph266pay, "PS(%u,%u) replaced", type, id);

  rtph266pay->ts_last_ps_to_sent = _get_nalu_running_time (rtph266pay, nalu);
}

static void
_clear_ps_cache (GstRtpH266Pay * rtph266pay)
{
  rtph266pay->ts_last_ps_to_sent = GST_CLOCK_TIME_NONE;
  g_hash_table_remove_all (rtph266pay->ps_id_nalu_map);
}

static gboolean
_can_insert_ps (GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  if (_nalu_is_rsv (nalu) || !NALU_IS_VCL (nalu))
    return FALSE;               // Can't insert before this kind of NALU

  GstClockTime ts_nalu = _get_nalu_running_time (rtph266pay, nalu);
  GstClockTime ts_last_ps = rtph266pay->ts_last_ps_to_sent;
  gboolean ts_last_ps_valid = GST_CLOCK_TIME_IS_VALID (ts_last_ps);
  gboolean automatic_interval = rtph266pay->config_interval < 0;
  gboolean ps_already_sent = ts_last_ps_valid && (ts_last_ps == ts_nalu);

  if (automatic_interval) {
    if (!NALU_IS_IDR (nalu) || ps_already_sent)
      return FALSE;
    GST_DEBUG_OBJECT (rtph266pay, "IDR detected: PS can be sent");
    return TRUE;
  } else {
    if (!ts_last_ps_valid)
      return FALSE;             // Haven't saw any PS yet

    GstClockTime ps_period = rtph266pay->config_interval * GST_SECOND;
    GstClockTime ts_next_ps = ts_last_ps + ps_period;

    if (ts_next_ps <= ts_nalu) {
      GST_DEBUG_OBJECT (rtph266pay, "config-interval starved: PS can be sent");
      return TRUE;
    }
  }

  return FALSE;
}

static void
_insert_ps_cache (GstRtpH266Pay * rtph266pay)
{
  if (g_hash_table_size (rtph266pay->ps_id_nalu_map) == 0)
    return;
  if (rtph266pay->config_interval == 0)
    return;                     // Disabled

  GQueue *nalus = &rtph266pay->nalus;
  GList *head = g_queue_peek_head_link (nalus);

  for (GList * l = head; l != NULL; l = l->next) {
    Nalu *current_nalu = l->data;

    if (!_can_insert_ps (rtph266pay, current_nalu))
      continue;

    // Insert all cached parameter sets before the current nalu
    GHashTableIter iter;
    Nalu *ps_nalu;
    g_hash_table_iter_init (&iter, rtph266pay->ps_id_nalu_map);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer) & ps_nalu)) {
      Nalu *ps_nalu_copy = _nalu_copy (ps_nalu);
      ps_nalu_copy = _nalu_copy_ts (ps_nalu_copy, current_nalu);
      GST_DEBUG_OBJECT (rtph266pay, "PS(%u,%u) queued to be sent",
          ps_nalu_copy->type, ps_nalu_copy->ps_id);
      g_queue_insert_before (nalus, l, ps_nalu_copy);
    }

    // Update ts_last_ps_to_sent with the PTS of the current NALU
    rtph266pay->ts_last_ps_to_sent =
        _get_nalu_running_time (rtph266pay, current_nalu);

    break;                      // PS already inserted
  }
}

static void
_set_au_boundaries (GstRtpH266Pay * rtph266pay)
{
  GList *head = g_queue_peek_head_link (&rtph266pay->nalus);
  Nalu *last_nalu = g_queue_peek_tail (&rtph266pay->nalus);
  // Take into account the last aggregated NALU
  Nalu *prev_nalu = g_queue_peek_tail (&rtph266pay->aggregated_nalus.nalus);

  for (GList * l = head; l != NULL; l = l->next) {
    Nalu *nalu = l->data;
    prev_nalu = l->prev ? l->prev->data : prev_nalu;
    //gboolean discont = GST_BUFFER_IS_DISCONT (nalu->nalu_buf);
    GstClockTime prev_pts = GST_CLOCK_TIME_NONE;
    GstClockTime prev_dts = GST_CLOCK_TIME_NONE;
    GstClockTime pts = GST_BUFFER_PTS (nalu->nalu_buf);
    GstClockTime dts = GST_BUFFER_DTS (nalu->nalu_buf);
    if (prev_nalu) {
      prev_pts = GST_BUFFER_PTS (prev_nalu->nalu_buf);
      prev_dts = GST_BUFFER_DTS (prev_nalu->nalu_buf);
    }
    gboolean aud = nalu->type == NALU_TYPE_AUD_NUT;
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
  AggregatedNalus *aggregated_nalus = &rtph266pay->aggregated_nalus;
  GQueue *nalus = &rtph266pay->nalus;
  GstFlowReturn ret = GST_FLOW_OK;
  Nalu *nalu;

  // FIXME: Maybe it's worth to avoid iterating the NALUs several times, maybe not
  _insert_ps_cache (rtph266pay);
  _set_au_boundaries (rtph266pay);

  // 1. Aggregate NALUs while possible
  // ---- vvv Can't aggregate anymore vvv ----
  // 2. If it has been possible to **aggregate something**:
  //   1. Keep the current NALU, it'll be processed later
  //   2. Send the current Aggregation Packet
  // 3. If it has been possible to **aggregate nothing**:
  //   * If the packet fits into a MTP -> Send a Unit Packet
  //   * Else -> Send two or more Fragmentation Units
  //
  // Note: _push_aggregated will send a UP when there is only 1 NALU aggregated
  while ((nalu = g_queue_pop_head (nalus))) {
    // Aggregate NALUs while possible
    if (_can_aggregate_nalu (rtph266pay, nalu)) {
      _aggregated_nalus_add (aggregated_nalus, nalu);
      GST_DEBUG_OBJECT (rtph266pay, "NALU aggregated: %" NALU_PTR_FORMAT,
          NALU_ARGS (nalu));
      continue;
    }

    // vvv Can't aggregate anymore vvv
    if (!_aggregated_nalus_is_empty (aggregated_nalus)) {
      g_queue_push_head (nalus, nalu);  // Keep this one for later
      ret = _push_aggregated (rtph266pay);      // Push what we have right now
      if (ret != GST_FLOW_OK)
        return ret;
    } else {                    // no-aggregation path
      gboolean is_last = g_queue_get_length (nalus) == 0;
      // Can't push the last NALU without knowing if it's the end of an AU,
      // because setting the M bit could be necessary. But have to push it if
      // we're on EOS
      if (!eos && is_last && !nalu->au_end) {
        GST_DEBUG_OBJECT (rtph266pay, "Keeping last NALU: %" NALU_PTR_FORMAT,
            NALU_ARGS (nalu));
        g_queue_push_head (nalus, nalu);
        return GST_FLOW_OK;
      }

      GST_DEBUG_OBJECT (rtph266pay, "Pushing NALU: %" NALU_PTR_FORMAT,
          NALU_ARGS (nalu));

      if (_up_fits_in_mtu (rtph266pay, nalu))
        ret = _push_unit_pkt (rtph266pay, nalu);
      else
        ret = _push_fragmented (rtph266pay, nalu);

      if (ret != GST_FLOW_OK)
        return ret;
    }
  }

  // If we already have an Access Unit aggregated, sent it right now instead of
  // waiting for the next buffer. An AP can't have more than 1 AU aggregated.
  if (_aggregated_nalus_have_au (aggregated_nalus))
    return _push_aggregated (rtph266pay);
  return GST_FLOW_OK;
}

static GstFlowReturn
_push_unit_pkt (GstRtpH266Pay * rtph266pay, Nalu * nalu)
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
  _nalu_free (nalu);
  GST_DEBUG_OBJECT (rtph266pay, "Pushing UP %" GST_PTR_FORMAT, out_buf);
  return gst_rtp_base_payload_push (rtpbasepay, out_buf);
error:
  _nalu_free (nalu);
  gst_buffer_unref (out_buf);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
_push_fragmented (GstRtpH266Pay * rtph266pay, Nalu * nalu)
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
  _nalu_free (nalu);
  return gst_rtp_base_payload_push_list (rtpbasepay, out_buflist);
}

static GstFlowReturn
_push_aggregated (GstRtpH266Pay * rtph266pay)
{
  GstRTPBasePayload *rtpbasepay = GST_RTP_BASE_PAYLOAD (rtph266pay);
  AggregatedNalus *aggregated_nalus = &rtph266pay->aggregated_nalus;
  GQueue *agg_nalus = &aggregated_nalus->nalus;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *out_buf;
  guint num_nalus;
  Nalu *nalu;

  // Can't send an AP with a single NALU
  num_nalus = g_queue_get_length (agg_nalus);
  if (num_nalus == 1) {
    GST_DEBUG_OBJECT (rtph266pay, "Can't send an AP with a single NALU");
    nalu = g_queue_pop_head (agg_nalus);
    _aggregated_nalus_clear (aggregated_nalus);
    return _push_unit_pkt (rtph266pay, nalu);
  }

  // Allocate just the RTP header. We'll add the buffer payload later. This way
  // we avoid unnecessary copies
  out_buf = gst_rtp_base_payload_allocate_output_buffer (rtpbasepay, 0, 0, 0);
  if (!gst_rtp_buffer_map (out_buf, GST_MAP_WRITE, &rtp))
    goto error;

  // Copy buffer metadata from the last nalu to aggregate
  nalu = g_queue_peek_tail (agg_nalus);
  gst_buffer_copy_into (out_buf, nalu->nalu_buf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  gst_rtp_buffer_set_marker (&rtp, nalu->au_end);

  // Append PayloadHdr (RFC 9328 4.3.2)
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |F|Z| MinLayerID| Type(28)| TID |
  // +---------------+---------------+
  guint16 f_bit_msk = (!!aggregated_nalus->f_bit) << (7 + 8);
  guint16 min_layer_id_msk = ((aggregated_nalus->min_layer_id & 0x3F) << 8);
  guint16 type_msk = AP_TYPE << 3;
  guint16 payload_hdr = g_htons (0 | f_bit_msk | min_layer_id_msk | type_msk);
  GstBuffer *payload_hdr_buf =
      gst_buffer_new_memdup (&payload_hdr, sizeof (payload_hdr));
  out_buf = gst_buffer_append (out_buf, payload_hdr_buf);

  // TODO: Conditionally append DONL

  // Append all NALUs with their sizes and headers
  while ((nalu = g_queue_pop_head (agg_nalus))) {
    guint16 nalu_size = g_htons (nalu->size);
    GstBuffer *nalu_size_buf =
        gst_buffer_new_memdup (&nalu_size, sizeof (nalu_size));

    out_buf = gst_buffer_append (out_buf, nalu_size_buf);
    out_buf = gst_buffer_append (out_buf, gst_buffer_ref (nalu->hdr_buf));
    out_buf = gst_buffer_append (out_buf, gst_buffer_ref (nalu->rbsp_buf));
    _nalu_free (nalu);
  }
  g_assert (out_buf);

  gst_rtp_buffer_unmap (&rtp);
  GST_DEBUG_OBJECT (rtph266pay, "Pushing AP [%d] %" GST_PTR_FORMAT, num_nalus,
      out_buf);
  _aggregated_nalus_clear (aggregated_nalus);
  return gst_rtp_base_payload_push (rtpbasepay, out_buf);
error:
  _aggregated_nalus_clear (aggregated_nalus);
  gst_buffer_unref (out_buf);
  return GST_FLOW_ERROR;
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

static gboolean
_ap_fits_in_mtu (const GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  const AggregatedNalus *aggregated_nalus = &rtph266pay->aggregated_nalus;
  guint mtu = GST_RTP_BASE_PAYLOAD_MTU (rtph266pay);
  guint num_nalus = g_queue_get_length ((GQueue *) & aggregated_nalus->nalus);
  gsize nalu_size_sum = aggregated_nalus->nalu_size_sum;
  guint nalu_size_field_sum = (num_nalus + 1) * sizeof (guint16);       // +1 for the current nalu
  // TODO: Consider DONL
  guint payload_size =
      NALU_HDR_LEN + nalu_size_field_sum + nalu_size_sum + nalu->size;

  gboolean fits = gst_rtp_buffer_calc_packet_len (payload_size, 0, 0) <= mtu;

  if (!fits) {
    GST_DEBUG_OBJECT (rtph266pay,
        "NALU does not fit into the current AP [%u/%u]: %" GST_PTR_FORMAT,
        payload_size, mtu, nalu->nalu_buf);
  }
  return fits;
}

static gboolean
_can_aggregate_nalu (GstRtpH266Pay * rtph266pay, Nalu * nalu)
{
  AggregateMode aggregate_mode = rtph266pay->aggregate_mode;

  if (aggregate_mode == AGGREGATE_NONE)
    return FALSE;

  if (aggregate_mode == AGGREGATE_ZERO_LATENCY) {
    if (NALU_IS_VCL (nalu)) {
      GST_DEBUG_OBJECT (rtph266pay, "VLC NALU -> can't aggregate");
      return FALSE;
    }
  }

  gboolean have_aggregated_nalus =
      !_aggregated_nalus_is_empty (&rtph266pay->aggregated_nalus);
  if (nalu->au_start && have_aggregated_nalus) {
    GST_DEBUG_OBJECT (rtph266pay, "More than 1 AU -> can't aggregate");
    return FALSE;
  }

  return _ap_fits_in_mtu (rtph266pay, nalu);
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

  // Setup PayloadHdr and FU Header (RFC 9328 4.3.3)
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
_clear_nalu_queues (GstRtpH266Pay * rtph266pay)
{
  g_queue_clear_full (&rtph266pay->nalus, (GDestroyNotify) _nalu_free);
  _aggregated_nalus_clear (&rtph266pay->aggregated_nalus);
}

static GstClockTime
_get_nalu_running_time (const GstRtpH266Pay * rtph266pay, const Nalu * nalu)
{
  GstSegment *segment = &GST_RTP_BASE_PAYLOAD (rtph266pay)->segment;
  GstClockTime pts = GST_BUFFER_PTS (nalu->nalu_buf);
  return gst_segment_to_running_time (segment, GST_FORMAT_TIME, pts);
}

static Nalu *
_nalu_new (GstBuffer * nalu_buf)
{
  // ITU-T H.266 V3: 7.3.1.2, 7.3.2.3, 7.3.2.4, 7.3.2.5, 7.3.2.6
  // |            NALU HDR           |  2 first bytes of PS
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
  nalu->f_bit = !!(data[0] & 0x80);
  nalu->layer_id = data[0] & 0x3F;
  nalu->type = data[1] >> 3;
  switch (nalu->type) {
    case NALU_TYPE_VPS_NUT:
    case NALU_TYPE_SPS_NUT:
      nalu->ps_id = data[2] >> 4;
      break;
    case NALU_TYPE_PPS_NUT:
      nalu->ps_id = data[2] >> 2;
      break;
    case NALU_TYPE_PREFIX_APS_NUT:
    case NALU_TYPE_SUFFIX_APS_NUT:
      nalu->ps_id = ((data[2] & 0x03) << 2) | ((data[3] & 0xC0) >> 6);
      break;
    default:
      nalu->ps_id = NALU_INVALID_PS;
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

static Nalu *
_nalu_copy (const Nalu * nalu)
{
  Nalu *new_nalu = g_new (Nalu, 1);
  *new_nalu = *nalu;
  new_nalu->nalu_buf = gst_buffer_ref (nalu->nalu_buf);
  new_nalu->hdr_buf = gst_buffer_ref (nalu->hdr_buf);
  new_nalu->rbsp_buf = gst_buffer_ref (nalu->rbsp_buf);
  return new_nalu;
}

static Nalu *
_nalu_copy_ts (Nalu * dst, const Nalu * src)
{
  dst->nalu_buf = gst_buffer_make_writable (dst->nalu_buf);
  gst_buffer_copy_into (dst->nalu_buf, src->nalu_buf,
      GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  return dst;
}

static gboolean
_nalu_is_rsv (const Nalu * nalu)
{
  switch (nalu->type) {
    case NALU_TYPE_RSV_VCL_4:
    case NALU_TYPE_RSV_VCL_5:
    case NALU_TYPE_RSV_VCL_6:
    case NALU_TYPE_RSV_IRAP_11:
    case NALU_TYPE_RSV_NVCL_26:
    case NALU_TYPE_RSV_NVCL_27:
      return TRUE;
    default:
      return FALSE;
  }
}

static void
_aggregated_nalus_init (AggregatedNalus * aggregated_nalus)
{
  g_queue_init (&aggregated_nalus->nalus);
  aggregated_nalus->nalu_size_sum = 0;
  aggregated_nalus->min_layer_id = 0;
  aggregated_nalus->f_bit = FALSE;
}

static void
_aggregated_nalus_clear (AggregatedNalus * aggregated_nalus)
{
  g_queue_clear_full (&aggregated_nalus->nalus, (GDestroyNotify) _nalu_free);
  aggregated_nalus->nalu_size_sum = 0;
  aggregated_nalus->min_layer_id = 0;
  aggregated_nalus->f_bit = FALSE;
}

static void
_aggregated_nalus_add (AggregatedNalus * aggregated_nalus, Nalu * nalu)
{
  g_queue_push_head (&aggregated_nalus->nalus, nalu);
  aggregated_nalus->nalu_size_sum += nalu->size;
  if (nalu->layer_id < aggregated_nalus->min_layer_id)
    aggregated_nalus->min_layer_id = nalu->layer_id;
  aggregated_nalus->f_bit = aggregated_nalus->f_bit || nalu->f_bit;
}

static gboolean
_aggregated_nalus_is_empty (AggregatedNalus * aggregated_nalus)
{
  return g_queue_get_length (&aggregated_nalus->nalus) == 0;
}

static gboolean
_aggregated_nalus_have_au (AggregatedNalus * aggregated_nalus)
{
  Nalu *last_nalu = g_queue_peek_tail (&aggregated_nalus->nalus);
  return last_nalu && last_nalu->au_end;
}

static GType
_aggregate_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {AGGREGATE_NONE, "Do not aggregate NAL units", "none"},
    {AGGREGATE_ZERO_LATENCY,
          "Aggregate NAL units until a VCL or suffix unit is included",
        "zero-latency"},
    {AGGREGATE_MAX,
          "Aggregate all NAL units with the same timestamp (adds one frame of latency)",
        "max"},
    {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static ("GstRtpH266AggregateMode", values);
  }
  return type;
}

static gboolean
_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstRtpH266Pay *rtph266pay = GST_RTP_H266_PAY (parent);

  if (GST_QUERY_TYPE (query) != GST_QUERY_LATENCY)
    return gst_pad_query_default (pad, parent, query);

  if (rtph266pay->alignment == ALIGNMENT_UNKNOWN)
    return FALSE;

  gboolean aggregate_max = rtph266pay->aggregate_mode == AGGREGATE_MAX;
  gboolean au_alignment = rtph266pay->alignment == ALIGNMENT_AU;
  gint fps_n = rtph266pay->fps_n;
  gint fps_d = rtph266pay->fps_d;
  gboolean configured = fps_n && fps_d;
  if (!aggregate_max || !au_alignment || !configured)
    return gst_pad_query_default (pad, parent, query);

  GstClockTime min_latency;
  GstClockTime max_latency;
  gboolean live;
  gst_query_parse_latency (query, &live, &min_latency, &max_latency);

  GstClockTime one_frame = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  min_latency += one_frame;
  max_latency += one_frame;

  gst_query_set_latency (query, live, min_latency, max_latency);
  return gst_pad_query_default (pad, parent, query);
}
