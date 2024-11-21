/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2014> Jurgen Slowack <jurgenslowack@gmail.com>
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

/* references:
 * - RTP Payload Format for Versatile Video Coding (VVC)
 *   https://www.ietf.org/rfc/rfc9328.txt
 * - RTP: A Transport Protocol for Real-Time Applications
 *   https://www.ietf.org/rfc/rfc3550.txt
 * - H.266: Versatile video coding
 *   https://www.itu.int/rec/T-REC-H.266-202309-I
 */

#include "gstrtph266depay.h"
#include <gst/rtp/gstrtppayloads.h>
#include <gst/rtp/gstrtpelements.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtputils.h>

#define NAL_TYPE_IS_PARAMETER_SET(nt) (((nt) == GST_H266_NAL_VPS)\
										||  ((nt) == GST_H266_NAL_SPS)\
										||  ((nt) == GST_H266_NAL_PPS)				)
#define NAL_TYPE_IS_KEY(nt) (NAL_TYPE_IS_PARAMETER_SET(nt))

#define DEFAULT_CONFIG_INTERVAL 0
#define DEFAULT_CLOCK_RATE 90000

#define GST_CAT_DEFAULT rtph266depay_debug
#define gst_rtp_h266_depay_parent_class parent_class

GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
G_DEFINE_TYPE (GstRtpH266Depay, gst_rtp_h266_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtph266depay, "rtph266depay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_H266_DEPAY, rtp_element_init (plugin));

static GstStaticPadTemplate gst_rtp_h266_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h266, stream-format = (string) byte-stream, alignment = (string) au "));

static GstStaticPadTemplate gst_rtp_h266_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H266\""));

enum
{
  PROP_0,
};

/* 3 zero bytes syncword */
static const guint8 sync_bytes[] = { 0, 0, 0, 1 };

static gboolean
gst_rtp_h266_depay_negotiate (GstRtpH266Depay * rtph266depay)
{
  GstCaps *caps;
  gboolean ret = FALSE;

  caps =
      gst_pad_get_allowed_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (rtph266depay));

  GST_DEBUG_OBJECT (rtph266depay, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (!caps) {
    GST_ERROR_OBJECT (rtph266depay, "Caps not found.");
    return ret;
  }

  if (gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str;

    GST_DEBUG_OBJECT (rtph266depay, "get stream-format");
    str = gst_structure_get_string (s, "stream-format");
    if (g_strcmp0 (str, "byte-stream") != 0) {
      GST_ERROR_OBJECT (rtph266depay, "only byte-stream supported: %s", str);
      goto beach;
    }

    GST_DEBUG_OBJECT (rtph266depay, "st: %" GST_PTR_FORMAT, s);


    GST_DEBUG_OBJECT (rtph266depay, "get alignment");
    str = gst_structure_get_string (s, "alignment");
    if (g_strcmp0 (str, "au") == 0) {
      rtph266depay->alignment = GST_H266_ALIGNMENT_AU;
    } else {
      GST_ERROR_OBJECT (rtph266depay, "alignment not supported: %s", str);
      goto beach;
    }
  }

  ret = TRUE;

beach:
  gst_caps_unref (caps);
  return ret;
}

static GstBuffer *
gst_rtp_h266_depay_allocate_output_buffer (GstRtpH266Depay * depay, gsize size)
{
  GstBuffer *buffer = NULL;

  GST_LOG_OBJECT (depay, "want output buffer of %u bytes", (guint) size);

  g_return_val_if_fail (size > 0, NULL);

  // TODO: Forward allocator.
  buffer = gst_buffer_new_allocate (NULL, size, NULL);

  return buffer;
}

static GstBuffer *
gst_rtp_h266_complete_au (GstRtpH266Depay * rtph266depay,
    GstClockTime * out_timestamp, gboolean * out_keyframe)
{
  GstBufferList *list;
  GstMapInfo outmap;
  GstBuffer *outbuf;
  guint outsize, offset = 0;
  gint b, n_bufs, m, n_mem;

  /* we had a picture in the adapter and we completed it */
  GST_DEBUG_OBJECT (rtph266depay, "taking completed AU");
  outsize = gst_adapter_available (rtph266depay->picture_adapter);

  GST_DEBUG_OBJECT (rtph266depay, "will allocate buffer of size %d", outsize);
  outbuf = gst_rtp_h266_depay_allocate_output_buffer (rtph266depay, outsize);

  if (G_UNLIKELY (outbuf == NULL))
    return NULL;

  if (!gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE))
    return NULL;

  list = gst_adapter_take_buffer_list (rtph266depay->picture_adapter, outsize);

  n_bufs = gst_buffer_list_length (list);
  for (b = 0; b < n_bufs; ++b) {
    GstBuffer *buf = gst_buffer_list_get (list, b);

    n_mem = gst_buffer_n_memory (buf);
    for (m = 0; m < n_mem; ++m) {
      GstMemory *mem = gst_buffer_peek_memory (buf, m);
      gsize mem_size = gst_memory_get_sizes (mem, NULL, NULL);
      GstMapInfo mem_map;

      if (gst_memory_map (mem, &mem_map, GST_MAP_READ)) {
        memcpy (outmap.data + offset, mem_map.data, mem_size);
        gst_memory_unmap (mem, &mem_map);
      } else {
        memset (outmap.data + offset, 0, mem_size);
      }
      offset += mem_size;
    }

    gst_rtp_copy_video_meta (rtph266depay, outbuf, buf);
  }
  gst_buffer_list_unref (list);
  gst_buffer_unmap (outbuf, &outmap);

  *out_timestamp = rtph266depay->last_ts;
  *out_keyframe = rtph266depay->last_keyframe;

  rtph266depay->last_keyframe = FALSE;

  return outbuf;
}


static void
gst_rtp_h266_depay_push (GstRtpH266Depay * rtph266depay, GstBuffer * outbuf,
    gboolean keyframe, GstClockTime timestamp, gboolean marker)
{
  GST_DEBUG_OBJECT (rtph266depay, "To push buffer");

  outbuf = gst_buffer_make_writable (outbuf);

  gst_rtp_drop_non_video_meta (rtph266depay, outbuf);

  GST_BUFFER_PTS (outbuf) = timestamp;

  if (keyframe)
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

  if (marker)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_MARKER);

  gst_rtp_base_depayload_push (GST_RTP_BASE_DEPAYLOAD (rtph266depay), outbuf);
}


static void
gst_rtp_h266_depay_handle_nal (GstRtpH266Depay * rtph266depay, GstBuffer * nal,
    GstClockTime in_timestamp, gboolean marker)
{
  GstRTPBaseDepayload *depayload = GST_RTP_BASE_DEPAYLOAD (rtph266depay);
  GstBuffer *outbuf = NULL;
  GstMapInfo map;
  gboolean keyframe, out_keyframe;
  GstClockTime out_timestamp;
  guint8 nal_unit_type;

  gst_buffer_map (nal, &map, GST_MAP_READ);
  if (G_UNLIKELY (map.size <= sizeof (sync_bytes)))
    goto short_nal;

  GST_MEMDUMP_OBJECT (rtph266depay, "nal data: ", map.data, map.size);

  nal_unit_type = (map.data[sizeof (sync_bytes) + 1] >> 3) & 0x1F;
  GST_DEBUG_OBJECT (rtph266depay, "Process nal with type %d", nal_unit_type);

  g_assert (rtph266depay->alignment == GST_H266_ALIGNMENT_AU);

  keyframe = NAL_TYPE_IS_PARAMETER_SET (nal_unit_type);
  out_keyframe = keyframe;
  out_timestamp = in_timestamp;

#if 0
  /* Assume payloader always sets the (marker) M bit (whether 0 or 1). */
  if (!marker) {
    /* ... */
  }
#endif

  /* add to adapter */
  gst_buffer_unmap (nal, &map);
  GST_DEBUG_OBJECT (depayload, "adding NAL to picture adapter");
  gst_adapter_push (rtph266depay->picture_adapter, nal);
  rtph266depay->last_ts = in_timestamp;
  rtph266depay->last_keyframe = rtph266depay->last_keyframe || keyframe;

  if (marker)
    outbuf = gst_rtp_h266_complete_au (rtph266depay, &out_timestamp,
        &out_keyframe);

  if (outbuf) {
    gst_rtp_h266_depay_push (rtph266depay, outbuf, out_keyframe, out_timestamp,
        marker);
  }

  return;

  /* ERRORS */
short_nal:
  {
    GST_WARNING_OBJECT (depayload, "dropping short NAL");
    gst_buffer_unmap (nal, &map);
    gst_buffer_unref (nal);
    return;
  }
}

static void
gst_rtp_h266_finish_fragmentation_unit (GstRtpH266Depay * rtph266depay)
{
  guint outsize;
  GstBuffer *outbuf;

  outsize = gst_adapter_available (rtph266depay->adapter);
  g_assert (outsize >= sizeof (sync_bytes));

  outbuf = gst_adapter_take_buffer (rtph266depay->adapter, outsize);
  GST_DEBUG_OBJECT (rtph266depay, "output %d bytes", outsize);

#if 0
  {
    GstMapInfo map;

    gst_buffer_map (outbuf, &map, GST_MAP_READ);
    g_assert (g_memcmp (map.data, sync_bytes, sizeof (sync_bytes)) == 0);
    gst_buffer_unmap (outbuf, &map);
  }
#endif

  rtph266depay->current_fu_type = 0;

  gst_rtp_h266_depay_handle_nal (rtph266depay, outbuf,
      rtph266depay->fu_timestamp, rtph266depay->fu_marker);
}

static GstBuffer *
gst_rtp_h266_depay_process (GstRTPBaseDepayload * depayload, GstRTPBuffer * rtp)
{
  GstRtpH266Depay *rtph266depay = GST_RTP_H266_DEPAY (depayload);
  GstBuffer *outbuf = NULL;
  GstMapInfo map;
  gint payload_len;
  guint8 *payload;
  guint8 nal_unit_type, nuh_layer_id, nuh_temporal_id_plus1;
  guint header_len;
  GstClockTime timestamp;
  gboolean marker;
  guint outsize, nalu_size;

  GST_DEBUG_OBJECT (depayload, "Start processing.");

  payload_len = gst_rtp_buffer_get_payload_len (rtp);
  payload = gst_rtp_buffer_get_payload (rtp);

  GST_DEBUG_OBJECT (rtph266depay, "receiving %d bytes", payload_len);

  if (payload_len == 0)
    goto empty_packet;

  // +---------------+---------------+
  // |0|1|2|3|4|5|6|7|0|1|2|3|4|5|6|7|
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |F|Z| LayerID   |  Type   | TID |
  // +---------------+---------------+

  nal_unit_type = (payload[1] >> 3) & 0x1F;
  nuh_layer_id = payload[0] & 0x3F;
  nuh_temporal_id_plus1 = payload[1] & 0x07;

  header_len = 2;

  timestamp = GST_BUFFER_PTS (rtp->buffer);
  marker = gst_rtp_buffer_get_marker (rtp);

  GST_DEBUG_OBJECT (rtph266depay, "marker: %d", marker);
  GST_DEBUG_OBJECT (rtph266depay,
      "NAL header nal_unit_type %d, nuh_temporal_id_plus1 %d", nal_unit_type,
      nuh_temporal_id_plus1);
  GST_DEBUG_OBJECT (depayload, "is discont %d",
      GST_BUFFER_IS_DISCONT (rtp->buffer));

#if 0
  GST_FIXME_OBJECT (rtph266depay, "Assuming DONL field is not present");
#endif

  /* If FU unit was being processed, but the current nal is of a different
   * type.  Assume that the remote payloader is buggy (didn't set the end bit
   * when the FU ended) and send out what we gathered thusfar */
  if (G_UNLIKELY (rtph266depay->current_fu_type != 0 &&
          nal_unit_type != rtph266depay->current_fu_type)) {
    gst_rtp_base_depayload_delayed (depayload);
    gst_rtp_h266_finish_fragmentation_unit (rtph266depay);
  }

  switch (nal_unit_type) {
    case GST_H266_NAL_AP:
    {
      goto not_implemented;
    }
    case GST_H266_NAL_FU:
    {
      guint8 S, E, P, FUType;
      guint16 nal_header;
      guint16 seqnum;

      GST_DEBUG_OBJECT (rtph266depay, "Processing Fragmentation Unit");
      // Fragmentation units (FUs)  Section 4.3.3
      //
      //  0                   1                   2                   3
      //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      // |   PayloadHdr (Type=29)        |   FU header   | DONL (cond)   |
      // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
      // |   DONL (cond) |                                               |
      // |-+-+-+-+-+-+-+-+                                               |
      // |                         FU payload                            |
      // |                                                               |
      // |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      // |                               :...OPTIONAL RTP padding        |
      // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      //
      // FU Header
      // +---------------+
      // |0|1|2|3|4|5|6|7|
      // +-+-+-+-+-+-+-+-+
      // |S|E|P|  FuType |
      // +---------------+

      /* strip headers */
      payload += header_len;    // Set pointer to the start of FU header.
      payload_len -= header_len;

      /* processing FU header */
      S = (payload[0] & 0x80) == 0x80;
      E = (payload[0] & 0x40) == 0x40;
      P = (payload[0] & 0x20) == 0x20;
      FUType = payload[0] & 0x1F;

      GST_DEBUG_OBJECT (rtph266depay,
          "FU header with S %d, E %d, P %d FUType %d", S, E, P, FUType);

      seqnum = gst_rtp_buffer_get_seq (rtp);

      if (S) {                  // Start of NAL unit.
        /* If a new FU unit started, while still processing an older one.
         * Assume that the remote payloader is buggy (doesn't set the end
         * bit) and send out what we've gathered thusfar */
        if (G_UNLIKELY (rtph266depay->current_fu_type != 0)) {
          gst_rtp_base_depayload_delayed (depayload);
          gst_rtp_h266_finish_fragmentation_unit (rtph266depay);
        }

        rtph266depay->current_fu_type = nal_unit_type;
        rtph266depay->fu_timestamp = timestamp;
        rtph266depay->last_fu_seqnum = seqnum;

        /* reconstruct NAL header */
        nal_header =
            (((guint16) FUType) << 3) | (((guint16) nuh_layer_id) << 8) |
            (guint16) nuh_temporal_id_plus1;
        GST_MEMDUMP_OBJECT (rtph266depay, "nal_header", (guint8 *) & nal_header,
            sizeof (nal_header));

        /* go back one byte so we can copy the payload + two bytes more in the front which
         * will be overwritten by the nal_header
         */
        payload -= 1;
        payload_len += 1;

        nalu_size = payload_len;
        outsize = nalu_size + sizeof (sync_bytes);
        outbuf = gst_buffer_new_and_alloc (outsize);

        gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
        memcpy (map.data, sync_bytes, sizeof (sync_bytes));
        memcpy (map.data + sizeof (sync_bytes), payload, nalu_size);
        map.data[sizeof (sync_bytes)] = nal_header >> 8;
        map.data[sizeof (sync_bytes) + 1] = nal_header & 0xff;
        gst_buffer_unmap (outbuf, &map);

      } else {
        if (rtph266depay->current_fu_type == 0) {
          /* previous FU packet missing start bit? */
          GST_WARNING_OBJECT (rtph266depay, "missing FU start bit on an "
              "earlier packet. Dropping.");
          gst_rtp_base_depayload_flush (depayload, FALSE);
          gst_adapter_clear (rtph266depay->adapter);
          return NULL;
        }
        if (gst_rtp_buffer_compare_seqnum (rtph266depay->last_fu_seqnum,
                seqnum) != 1) {
          /* jump in sequence numbers within an FU is cause for discarding */
          GST_WARNING_OBJECT (rtph266depay, "Jump in sequence numbers from "
              "%u to %u within Fragmentation Unit. Data was lost, dropping "
              "stored.", rtph266depay->last_fu_seqnum, seqnum);
          gst_rtp_base_depayload_flush (depayload, FALSE);
          gst_adapter_clear (rtph266depay->adapter);
          return NULL;
        }

        rtph266depay->last_fu_seqnum = seqnum;

        GST_DEBUG_OBJECT (rtph266depay, "FU seqnum: %d",
            rtph266depay->last_fu_seqnum);

        /* strip off FU header byte: Ignore DONL */
        payload += 1;
        payload_len -= 1;

        outsize = payload_len;
        outbuf = gst_buffer_new_and_alloc (outsize);
        gst_buffer_fill (outbuf, 0, payload, outsize);
      }

      gst_rtp_copy_video_meta (rtph266depay, outbuf, rtp->buffer);
      GST_DEBUG_OBJECT (rtph266depay, "queueing %d bytes", outsize);
      /* and assemble in the adapter */
      gst_adapter_push (rtph266depay->adapter, outbuf);
      outbuf = NULL;

      rtph266depay->fu_marker = marker;
      /* if NAL unit ends, flush the adapter */
      if (E) {
        gst_rtp_h266_finish_fragmentation_unit (rtph266depay);
        GST_DEBUG_OBJECT (rtph266depay, "End of Fragmentation Unit");
      }

      break;
    }
    default:
      GST_DEBUG_OBJECT (rtph266depay, "Processing Single NAL Unit packet");
      nalu_size = payload_len;
      outsize = nalu_size + sizeof (sync_bytes);
      outbuf = gst_buffer_new_and_alloc (outsize);

      gst_buffer_map (outbuf, &map, GST_MAP_WRITE);

      /* Assume byte-stream format. This is what template caps accepts right now. */
      memcpy (map.data, sync_bytes, sizeof (sync_bytes));
      memcpy (map.data + 4, payload, nalu_size);
      gst_buffer_unmap (outbuf, &map);

      gst_rtp_copy_video_meta (rtph266depay, outbuf, rtp->buffer);
      gst_rtp_h266_depay_handle_nal (rtph266depay, outbuf, timestamp, marker);
      break;
  }

  return NULL;

  /* ERRORS */
empty_packet:
  {
    GST_DEBUG_OBJECT (rtph266depay, "empty packet");
    gst_rtp_base_depayload_dropped (depayload);
    return NULL;
  }
not_implemented:
  {
    GST_ELEMENT_ERROR (rtph266depay, STREAM, FORMAT,
        (NULL), ("NAL unit type %d not supported yet", nal_unit_type));
    gst_rtp_base_depayload_dropped (depayload);
    return NULL;
  }

}

static void
gst_rtp_h266_depay_drain (GstRtpH266Depay * rtph266depay)
{
  GstClockTime timestamp;
  gboolean keyframe;
  GstBuffer *outbuf;

  outbuf = gst_rtp_h266_complete_au (rtph266depay, &timestamp, &keyframe);
  if (outbuf)
    gst_rtp_h266_depay_push (rtph266depay, outbuf, keyframe, timestamp, FALSE);
}

static void
gst_rtp_h266_depay_reset (GstRtpH266Depay * rtph266depay)
{
  gst_adapter_clear (rtph266depay->adapter);
  gst_adapter_clear (rtph266depay->picture_adapter);

  rtph266depay->last_keyframe = FALSE;
  rtph266depay->last_ts = 0;
  rtph266depay->current_fu_type = 0;
}

static gboolean
gst_rtp_h266_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstRtpH266Depay *rtph266depay = GST_RTP_H266_DEPAY (depayload);
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint clock_rate;

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = DEFAULT_CLOCK_RATE;
  depayload->clock_rate = clock_rate;

  if (!gst_rtp_h266_depay_negotiate (rtph266depay))
    return FALSE;

  GST_DEBUG_OBJECT (rtph266depay, "set caps");

  return TRUE;
}

static gboolean
gst_rtp_h266_depay_handle_event (GstRTPBaseDepayload * depay, GstEvent * event)
{
  GstRtpH266Depay *rtph266depay = GST_RTP_H266_DEPAY (depay);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_rtp_h266_depay_reset (rtph266depay);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (rtph266depay, "EOS...");
      gst_rtp_h266_depay_drain (rtph266depay);
      break;
    default:
      break;
  }

  return GST_RTP_BASE_DEPAYLOAD_CLASS (parent_class)->handle_event (depay,
      event);
}

static GstStateChangeReturn
gst_rtp_h266_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpH266Depay *rtph266depay = GST_RTP_H266_DEPAY (element);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED)
    gst_rtp_h266_depay_reset (rtph266depay);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    gst_rtp_h266_depay_reset (rtph266depay);

  return ret;
}

static void
gst_rtp_h266_depay_finalize (GObject * object)
{
  GstRtpH266Depay *rtph266depay = GST_RTP_H266_DEPAY (object);

  g_clear_pointer (&rtph266depay->adapter, g_object_unref);
  g_clear_pointer (&rtph266depay->picture_adapter, g_object_unref);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_h266_depay_class_init (GstRtpH266DepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_rtp_h266_depay_finalize);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_h266_depay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_h266_depay_sink_template);

  gstelement_class->change_state = gst_rtp_h266_depay_change_state;
  gstrtpbasedepayload_class->process_rtp_packet = gst_rtp_h266_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_h266_depay_setcaps;
  gstrtpbasedepayload_class->handle_event = gst_rtp_h266_depay_handle_event;

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP H266 depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts H266 video from RTP packets (RFC 9328)",
      "César Fabián Orccón Chipana <cfoch.fabian@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "rtph266depay", 0,
      "H266 RTP Depayloader");
}

static void
gst_rtp_h266_depay_init (GstRtpH266Depay * rtph266depay)
{
  rtph266depay->adapter = gst_adapter_new ();
  rtph266depay->picture_adapter = gst_adapter_new ();
}
