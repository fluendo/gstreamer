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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpegh3dasigner.h"
#include "gstmpegh3dahash.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

#define DEFAULT_HASH_METHOD GST_MPEGH3DA_HASH_SHA256
#define DEFAULT_AUTH_SEQUENCE_LENGTH 30
#define DEFAULT_AUTH_ID 0
#define DEFAULT_AU_COUNTER TRUE
#define DEFAULT_TIMESTAMP FALSE

/* Exact MHAS packet sizes for the packets we inject (type >= 32 => 3-byte
 * header, label 1). AUTH_START: 3 + 5; TIMESTAMP: 3 + 11. */
#define GST_MPEGH3DA_AUTH_START_SIZE 8
#define GST_MPEGH3DA_TIMESTAMP_SIZE 14

enum
{
  PROP_0,
  PROP_HASH_METHOD,
  PROP_AUTH_SEQUENCE_LENGTH,
  PROP_AUTH_ID,
  PROP_AU_COUNTER,
  PROP_TIMESTAMP,
  PROP_CONTENT_UUID,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "framed = (boolean) true, "
        "stream-format = (string) mhas, " "stream-type = (string) single"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "framed = (boolean) true, "
        "stream-format = (string) mhas, " "stream-type = (string) single"));

static void gst_mpegh3dasigner_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpegh3dasigner_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_mpegh3dasigner_generate_output (GstBaseTransform *
    trans, GstBuffer ** outbuf);
static gboolean gst_mpegh3dasigner_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_mpegh3dasigner_start (GstBaseTransform * trans);
static gboolean gst_mpegh3dasigner_stop (GstBaseTransform * trans);
static void gst_mpegh3dasigner_finalize (GObject * object);
static gboolean gst_mpegh3dasigner_parse_uuid (const gchar * str,
    guint8 out[16]);
static gsize gst_mpegh3dasigner_emit_auth_start (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size);
static gsize gst_mpegh3dasigner_emit_au_counter (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size, guint64 counter);
static gsize gst_mpegh3dasigner_emit_auth_sig (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size);
#define parent_class gst_mpegh3dasigner_parent_class
G_DEFINE_TYPE (GstMpegh3DASigner, gst_mpegh3dasigner, GST_TYPE_BASE_TRANSFORM);

static gsize
gst_mpegh3dasigner_au_counter_size (guint64 counter)
{
  gsize escaped;

  if (counter < 255)
    escaped = 1;
  else if (counter < 255 + 65535)
    escaped = 3;
  else
    escaped = 5;

  return 3 + 1 + escaped;
}

static gsize
gst_mpegh3dasigner_auth_sig_size (GstMpegh3daHashMethod method)
{
  return 6 + gst_mpegh3da_hash_digest_size (method);
}

/* Compute the exact output size for a buffer: the input packets plus the
 * packets that will be injected. Mirrors the injection logic of the splice
 * pass without mutating element state. */
static gsize
gst_mpegh3dasigner_compute_output_size (GstMpegh3DASigner * self,
    const GstMpegh3daMhasPacket * pkts, guint n_pkts)
{
  gboolean l_have_label = self->have_seq_label;
  gboolean l_finished = self->sequence_finished;
  guint64 l_label = self->seq_label;
  guint l_frame_count = self->frame_count;
  gsize size = 0;
  guint i;

  for (i = 0; i < n_pkts; i++) {
    const GstMpegh3daMhasPacket *pkt = &pkts[i];
    gboolean in_seq;

    if (!l_have_label && pkt->label != 0) {
      l_label = pkt->label;
      l_have_label = TRUE;
    }

    in_seq = l_have_label && pkt->label == l_label
        && !gst_mpegh3da_mhas_packet_type_is_excluded (pkt->type);

    if (in_seq && l_finished) {
      size += GST_MPEGH3DA_AUTH_START_SIZE;
      if (self->timestamp)
        size += GST_MPEGH3DA_TIMESTAMP_SIZE;
      l_finished = FALSE;
    }

    if (in_seq && pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME
        && self->au_counter) {
      size += gst_mpegh3dasigner_au_counter_size (l_frame_count);
    }

    size += pkt->header_size + pkt->length;

    if (in_seq && pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME) {
      l_frame_count++;
      if (l_frame_count >= self->auth_sequence_length)
        size += gst_mpegh3dasigner_auth_sig_size (self->hash_method);
    }
  }

  return size;
}

static GstFlowReturn
gst_mpegh3dasigner_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);
  GstBuffer *inbuf;
  GstMapInfo inmap, outmap;
  GstBitReader br;
  GArray *pkts;
  guint i;
  gsize out_size, out_off = 0;
  guint8 *out;
  const guint8 *pkt_start;
  gsize pkt_len;

  *outbuf = NULL;

  /* Take the input buffer stashed by the default submit_input_buffer. */
  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;
  if (inbuf == NULL)
    return GST_FLOW_OK;

  if (!gst_buffer_map (inbuf, &inmap, GST_MAP_READ)) {
    gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }

  pkts = g_array_new (FALSE, FALSE, sizeof (GstMpegh3daMhasPacket));

  /* Parse all packets (no side effects on element state). */
  gst_bit_reader_init (&br, inmap.data, (guint) inmap.size);
  while (gst_bit_reader_get_remaining (&br) != 0) {
    GstMpegh3daMhasPacket pkt;

    if (!gst_mpegh3da_mhas_parse_packet (&br, &pkt)) {
      GST_ERROR_OBJECT (trans, "malformed MHAS packet, stopping the parse");
      break;
    }
    gst_mpegh3da_mhas_log_packet (&pkt);
    g_array_append_val (pkts, pkt);
  }

  /* Exact output size. */
  out_size = gst_mpegh3dasigner_compute_output_size (self,
      (GstMpegh3daMhasPacket *) pkts->data, pkts->len);

  *outbuf = gst_buffer_new_allocate (NULL, out_size, NULL);
  if (!*outbuf) {
    g_array_free (pkts, TRUE);
    gst_buffer_unmap (inbuf, &inmap);
    gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }
  gst_buffer_copy_into (*outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS |
      GST_BUFFER_COPY_FLAGS, 0, 0);

  if (!gst_buffer_map (*outbuf, &outmap, GST_MAP_WRITE)) {
    g_array_free (pkts, TRUE);
    gst_buffer_unmap (inbuf, &inmap);
    gst_buffer_unref (inbuf);
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    return GST_FLOW_ERROR;
  }
  out = outmap.data;

  /* Splice pass: inject + hash in bitstream order, updating element state. */
  for (i = 0; i < pkts->len; i++) {
    const GstMpegh3daMhasPacket *pkt =
        &g_array_index (pkts, GstMpegh3daMhasPacket, i);

    pkt_start = pkt->payload - pkt->header_size;
    pkt_len = pkt->header_size + pkt->length;

    if (!self->have_seq_label && pkt->label != 0) {
      self->seq_label = pkt->label;
      self->have_seq_label = TRUE;
    }

    if (self->have_seq_label && pkt->label == self->seq_label
        && !gst_mpegh3da_mhas_packet_type_is_excluded (pkt->type)) {
      if (self->sequence_finished) {
        self->hash = gst_mpegh3da_hash_new (self->hash_method);
        if (!self->hash) {
          GST_ERROR_OBJECT (self, "failed to create hasher for hash-method %d",
              self->hash_method);
          g_array_free (pkts, TRUE);
          gst_buffer_unmap (*outbuf, &outmap);
          gst_buffer_unmap (inbuf, &inmap);
          gst_buffer_unref (inbuf);
          gst_buffer_unref (*outbuf);
          *outbuf = NULL;
          return GST_FLOW_ERROR;
        }
        self->frame_count = 0;
        self->sequence_finished = FALSE;
        out_off += gst_mpegh3dasigner_emit_auth_start (self,
            out + out_off, out_size - out_off);
      }

      if (pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME && self->au_counter) {
        out_off += gst_mpegh3dasigner_emit_au_counter (self,
            out + out_off, out_size - out_off, self->frame_count);
      }

      gst_mpegh3da_hash_update (self->hash, pkt_start, pkt_len);
    }

    memcpy (out + out_off, pkt_start, pkt_len);
    out_off += pkt_len;

    if (self->have_seq_label && pkt->label == self->seq_label
        && pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME) {
      self->frame_count++;
      if (self->frame_count >= self->auth_sequence_length)
        out_off += gst_mpegh3dasigner_emit_auth_sig (self,
            out + out_off, out_size - out_off);
    }
  }

  g_array_free (pkts, TRUE);
  gst_buffer_unmap (*outbuf, &outmap);
  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_unref (inbuf);

  return GST_FLOW_OK;
}

static gboolean
gst_mpegh3dasigner_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      if (self->hash) {
        gst_mpegh3da_hash_free (self->hash);
        self->hash = NULL;
      }
      self->frame_count = 0;
      self->have_seq_label = FALSE;
      self->seq_label = 0;
      self->sequence_finished = TRUE;
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_mpegh3dasigner_start (GstBaseTransform * trans)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);

  if (self->content_uuid != NULL && *self->content_uuid != '\0') {
    if (!gst_mpegh3dasigner_parse_uuid (self->content_uuid, self->uuid)) {
      GST_ERROR_OBJECT (self, "invalid content-uuid \"%s\"",
          self->content_uuid);
      return FALSE;
    }
    self->uuid_set = TRUE;
  } else {
    self->uuid_set = FALSE;
  }

  /* Verify the hash method is supported before streaming. The actual hasher
   * is created per-sequence in gst_mpegh3dasigner_start_sequence(). */
  {
    GstMpegh3daHash *probe = gst_mpegh3da_hash_new (self->hash_method);

    if (!probe) {
      GST_ERROR_OBJECT (self, "failed to create hasher for hash-method %d",
          self->hash_method);
      return FALSE;
    }
    gst_mpegh3da_hash_free (probe);
  }

  self->hash = NULL;
  self->frame_count = 0;
  self->have_seq_label = FALSE;
  self->seq_label = 0;
  self->sequence_finished = TRUE;

  return TRUE;
}

static gboolean
gst_mpegh3dasigner_stop (GstBaseTransform * trans)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);

  if (self->hash) {
    gst_mpegh3da_hash_free (self->hash);
    self->hash = NULL;
  }

  return TRUE;
}

static void
gst_mpegh3dasigner_finalize (GObject * object)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (object);

  g_free (self->content_uuid);
  self->content_uuid = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mpegh3dasigner_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (object);

  switch (prop_id) {
    case PROP_HASH_METHOD:
      self->hash_method = g_value_get_enum (value);
      break;
    case PROP_AUTH_SEQUENCE_LENGTH:
      self->auth_sequence_length = g_value_get_uint (value);
      break;
    case PROP_AUTH_ID:
      self->auth_id = g_value_get_uint (value);
      break;
    case PROP_AU_COUNTER:
      self->au_counter = g_value_get_boolean (value);
      break;
    case PROP_TIMESTAMP:
      self->timestamp = g_value_get_boolean (value);
      break;
    case PROP_CONTENT_UUID:
      g_free (self->content_uuid);
      self->content_uuid = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegh3dasigner_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (object);

  switch (prop_id) {
    case PROP_HASH_METHOD:
      g_value_set_enum (value, self->hash_method);
      break;
    case PROP_AUTH_SEQUENCE_LENGTH:
      g_value_set_uint (value, self->auth_sequence_length);
      break;
    case PROP_AUTH_ID:
      g_value_set_uint (value, self->auth_id);
      break;
    case PROP_AU_COUNTER:
      g_value_set_boolean (value, self->au_counter);
      break;
    case PROP_TIMESTAMP:
      g_value_set_boolean (value, self->timestamp);
      break;
    case PROP_CONTENT_UUID:
      g_value_set_string (value, self->content_uuid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegh3dasigner_class_init (GstMpegh3DASignerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_mpegh3dasigner_set_property;
  gobject_class->get_property = gst_mpegh3dasigner_get_property;
  gobject_class->finalize = gst_mpegh3dasigner_finalize;

  g_object_class_install_property (gobject_class, PROP_HASH_METHOD,
      g_param_spec_enum ("hash-method", "Hash method",
          "Hash algorithm to use (authHashType)",
          GST_TYPE_MPEGH3DA_HASH_METHOD, DEFAULT_HASH_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTH_SEQUENCE_LENGTH,
      g_param_spec_uint ("auth-sequence-length",
          "Authentication sequence length",
          "Number of access units per authentication sequence", 1,
          G_MAXUINT, DEFAULT_AUTH_SEQUENCE_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTH_ID,
      g_param_spec_uint ("auth-id", "Authentication ID",
          "Authentication ID (authID)", 0, 255, DEFAULT_AUTH_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AU_COUNTER,
      g_param_spec_boolean ("au-counter", "AU counter",
          "Emit PACTYP_AUTH_SEQUENCE_AU_COUNTER before each frame",
          DEFAULT_AU_COUNTER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMESTAMP,
      g_param_spec_boolean ("timestamp", "Timestamp",
          "Emit PACTYP_AUTH_TIMESTAMP (authTimeLong) once per sequence",
          DEFAULT_TIMESTAMP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONTENT_UUID,
      g_param_spec_string ("content-uuid", "Content UUID",
          "RFC 9562 UUID of the related (sub-)stream (canonical hyphenated "
          "form); empty disables the uuid field in the hash input", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "MPEG-H 3D Audio Media Authenticity Signer", "Filter/Audio",
      "Signs MPEG-H 3D audio (MHAS) streams with media authenticity",
      "Fluendo");

  transform_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_generate_output);
  transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_sink_event);
  transform_class->start = GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_start);
  transform_class->stop = GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_stop);
}

static void
gst_mpegh3dasigner_init (GstMpegh3DASigner * self)
{
  self->hash_method = DEFAULT_HASH_METHOD;
  self->auth_sequence_length = DEFAULT_AUTH_SEQUENCE_LENGTH;
  self->auth_id = DEFAULT_AUTH_ID;
  self->au_counter = DEFAULT_AU_COUNTER;
  self->timestamp = DEFAULT_TIMESTAMP;
  self->content_uuid = NULL;

  /* In-band byte insertion grows the buffers, so this is a non-in-place
   * transform. transform() is a no-op copy until the signing is implemented. */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), FALSE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_mpegh3daauth_debug, "mpegh3daauth", 0,
      "MPEG-H 3D Audio media authenticity signer/verifier");

  return gst_element_register (plugin, "mpegh3dasigner", GST_RANK_NONE,
      GST_TYPE_MPEGH3DASIGNER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpegh3daauth,
    "MPEG-H 3D Audio media authenticity signer/verifier",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
/* ---- helpers ---- */
/* Parse an RFC 9562 UUID in canonical hyphenated form
 * ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx") into 16 bytes. */
     static gboolean
         gst_mpegh3dasigner_parse_uuid (const gchar * str, guint8 out[16])
{
  guint i, nibbles = 0;

  if (!g_uuid_string_is_valid (str))
    return FALSE;

  for (i = 0; str[i] != '\0'; i++) {
    gint hi;

    if (str[i] == '-')
      continue;

    hi = g_ascii_xdigit_value (str[i]);
    if (hi < 0 || nibbles >= 32)
      return FALSE;

    if (nibbles % 2 == 0)
      out[nibbles / 2] = (guint8) (hi << 4);
    else
      out[nibbles / 2] |= (guint8) hi;

    nibbles++;
  }

  return nibbles == 32;
}

/* Emit PACTYP_AUTH_START (and PACTYP_TIMESTAMP when enabled) at the start of
 * a new authentication sequence. The emitted packets are part of gad_bytes,
 * so they are hashed here. Returns bytes written, or 0 on failure. */
static gsize
gst_mpegh3dasigner_emit_auth_start (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size)
{
  GstMpegh3daAuthStart cfg = { 0, };
  guint8 payload[128], pkt[160];
  gsize plen, tlen, written = 0;

  cfg.authID = self->auth_id;
  cfg.authHashType = self->hash_method;
  cfg.authKeyID = 0;
  cfg.authProvID = 1;
  cfg.authResilienceLevelMax = 0;
  cfg.hasAuthSequenceAUCounter = self->au_counter;
  cfg.isFirstSequence = 1;

  plen = gst_mpegh3da_build_AuthStart (&cfg, payload, sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (pkt, sizeof (pkt),
      GST_MPEGH3DA_PACTYP_AUTH_START, self->seq_label, payload, plen);
  if (tlen == 0 || tlen > out_size)
    return 0;

  gst_mpegh3da_hash_update (self->hash, pkt, tlen);
  memcpy (out, pkt, tlen);
  written = tlen;

  if (self->timestamp) {
    GstMpegh3daAuthTimestamp tcfg = { 0, };
    guint64 real_usec = g_get_real_time ();

    tcfg.authID = self->auth_id;
    tcfg.authTimeType = 0;
    tcfg.authTimeOffsetType = 0;
    tcfg.authTime = real_usec / G_USEC_PER_SEC - 1735689601ULL;
    tcfg.authTimeOffset = (guint16) ((real_usec / 1000) % 1000);

    plen = gst_mpegh3da_build_AuthTimestamp (&tcfg, payload, sizeof (payload));
    tlen = gst_mpegh3da_mhas_write_packet (pkt, sizeof (pkt),
        GST_MPEGH3DA_PACTYP_TIMESTAMP, self->seq_label, payload, plen);
    if (tlen == 0 || tlen > out_size - written)
      return 0;

    gst_mpegh3da_hash_update (self->hash, pkt, tlen);
    memcpy (out + written, pkt, tlen);
    written += tlen;
  }

  return written;
}

/* Emit PACTYP_AUTH_SEQUENCE_AU_COUNTER before a frame and hash it (it is part
 * of gad_bytes). Returns bytes written, or 0 on failure. */
static gsize
gst_mpegh3dasigner_emit_au_counter (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size, guint64 counter)
{
  GstMpegh3daAuthSequenceAUCounter cfg = { 0, };
  guint8 payload[16], pkt[32];
  gsize plen, tlen;

  cfg.authID = self->auth_id;
  cfg.authSequenceAUCounter = counter;

  plen = gst_mpegh3da_build_AuthSequenceAUCounter (&cfg, payload,
      sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (pkt, sizeof (pkt),
      GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER, self->seq_label, payload,
      plen);
  if (tlen == 0 || tlen > out_size)
    return 0;

  gst_mpegh3da_hash_update (self->hash, pkt, tlen);
  memcpy (out, pkt, tlen);

  return tlen;
}

/* Finalize the current sequence: append the uuid field (gad_bytes || uuid),
 * compute the digest and emit PACTYP_AUTH_SIG after the last frame. AUTH_SIG
 * is not part of gad_bytes. Returns bytes written, or 0 on failure. */
static gsize
gst_mpegh3dasigner_emit_auth_sig (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size)
{
  GstMpegh3daAuthSig cfg = { 0, };
  guint8 payload[128], pkt[160];
  guint8 digest[64];
  gsize digest_len = 0, plen, tlen, i;
  gchar hex[129];

  hex[0] = '\0';

  if (!self->hash) {
    self->sequence_finished = TRUE;
    return 0;
  }

  if (self->uuid_set)
    gst_mpegh3da_hash_update (self->hash, self->uuid, 16);

  gst_mpegh3da_hash_finish (self->hash, digest, &digest_len);

  for (i = 0; i < digest_len; i++)
    g_snprintf (hex + 2 * i, 3, "%02x", (guint) digest[i]);

  GST_DEBUG ("authentication sequence digest (label=%" G_GUINT64_FORMAT
      ", frames=%u): %s", self->seq_label, self->frame_count, hex);

  cfg.authID = self->auth_id;
  cfg.sigLengthMinus1 = (guint8) (digest_len - 1);
  memcpy (cfg.sigComplete, digest, digest_len);

  plen = gst_mpegh3da_build_AuthSig (&cfg, payload, sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (pkt, sizeof (pkt),
      GST_MPEGH3DA_PACTYP_AUTH_SIG, self->seq_label, payload, plen);
  if (tlen == 0 || tlen > out_size)
    return 0;

  memcpy (out, pkt, tlen);

  gst_mpegh3da_hash_free (self->hash);
  self->hash = NULL;
  self->frame_count = 0;
  self->sequence_finished = TRUE;

  return tlen;
}
