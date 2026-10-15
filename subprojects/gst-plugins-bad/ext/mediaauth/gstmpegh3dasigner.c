/* TODO: add copyright and license header */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmpegh3dasigner.h"
#include "gstmpegh3daparse.h"
#include "gstmpegh3dacrypto.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

#define DEFAULT_HASH_METHOD GST_MPEGH3DA_HASH_SHA256
#define DEFAULT_AUTH_SEQUENCE_LENGTH 30
#define DEFAULT_AUTH_ID 0
#define DEFAULT_AU_COUNTER TRUE
#define DEFAULT_TIMESTAMP FALSE

/* Worst-case bytes injected per output buffer: up to N deferred AUTH_SIG
 * segments plus one sequence start (AUTH_START + UUID) and one frame's
 * TIMESTAMP + AU_COUNTER. */
#define GST_MPEGH3DA_MAX_INJECT_PER_BUFFER \
  (GST_MPEGH3DA_MAX_AUTH_SIG_SEGMENTS * GST_MPEGH3DA_AUTH_SIG_MAX_PACKET_BYTES + \
   GST_MPEGH3DA_AUTH_START_MAX_PACKET_BYTES + \
   GST_MPEGH3DA_UUID_MAX_PACKET_BYTES + \
   GST_MPEGH3DA_TIMESTAMP_MAX_PACKET_BYTES + \
   GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PACKET_BYTES)

enum
{
  PROP_0,
  PROP_HASH_METHOD,
  PROP_AUTH_SEQUENCE_LENGTH,
  PROP_AUTH_ID,
  PROP_AU_COUNTER,
  PROP_TIMESTAMP,
  PROP_CONTENT_UUID,
  PROP_PRIVATE_KEY_PATH,
  PROP_PUBLIC_KEY_URI,
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
static GstFlowReturn gst_mpegh3dasigner_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_mpegh3dasigner_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize);
static gboolean gst_mpegh3dasigner_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_mpegh3dasigner_start (GstBaseTransform * trans);
static gboolean gst_mpegh3dasigner_stop (GstBaseTransform * trans);
static void gst_mpegh3dasigner_finalize (GObject * object);
static gboolean gst_mpegh3dasigner_begin_sequence (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size, gsize * out_off);
static gboolean gst_mpegh3dasigner_finish_sequence (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size, gsize * out_off);
static gsize gst_mpegh3dasigner_insert_auth_start (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size);
static gsize gst_mpegh3dasigner_insert_uuid (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size);
static gsize gst_mpegh3dasigner_insert_timestamp (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size);
static gsize gst_mpegh3dasigner_insert_au_counter (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size, guint64 counter);
static gsize gst_mpegh3dasigner_insert_auth_sig (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size);
#define parent_class gst_mpegh3dasigner_parent_class
G_DEFINE_TYPE (GstMpegh3DASigner, gst_mpegh3dasigner, GST_TYPE_BASE_TRANSFORM);

static gboolean
gst_mpegh3dasigner_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  /* One frame's worth of auth injection plus the deferred AUTH_SIG of the
   * previous sequence, on top of the input size. */
  *othersize = size + GST_MPEGH3DA_MAX_INJECT_PER_BUFFER;

  return TRUE;
}

static GstFlowReturn
gst_mpegh3dasigner_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo inmap, outmap;
  GstBitReader br;
  GArray *pkts = NULL;
  gboolean in_mapped = FALSE, out_mapped = FALSE;
  guint i, nframes = 0;
  gsize out_size, out_off = 0, emit_size;
  guint8 *out;
  const guint8 *pkt_start;
  gsize pkt_len;

  pkts = g_array_new (FALSE, FALSE, sizeof (GstMpegh3daMhasPacket));

  if (!gst_buffer_map (inbuf, &inmap, GST_MAP_READ))
    goto error;
  in_mapped = TRUE;

  self->buffer_count++;
  GST_INFO_OBJECT (trans, "=== buffer %" G_GUINT64_FORMAT
      " (input %" G_GSIZE_FORMAT " bytes) ===", self->buffer_count, inmap.size);

  /* Parse all packets (no side effects on element state). */
  gst_bit_reader_init (&br, inmap.data, (guint) inmap.size);
  while (gst_bit_reader_get_remaining (&br) != 0) {
    GstMpegh3daMhasPacket pkt;

    if (!gst_mpegh3da_mhas_parse_packet (&br, &pkt)) {
      GST_ERROR_OBJECT (trans, "malformed MHAS packet, dropping buffer");
      goto error;
    }

    if (pkt.type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME && ++nframes > 1) {
      GST_ERROR_OBJECT (trans, "buffer contains more than one MHAS frame");
      goto error;
    }

    gst_mpegh3da_mhas_log_packet (GST_OBJECT (trans), &pkt);
    g_array_append_val (pkts, pkt);
  }

  if (!gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE))
    goto error;
  out_mapped = TRUE;
  out = outmap.data;
  out_size = outmap.size;

  /* Deferred AUTH_SIG: finish the previous sequence at the very start of
   * this buffer (decoder-behaviour workaround test). */
  if (self->pending_auth_sig) {
    if (!gst_mpegh3dasigner_finish_sequence (self, out, out_size, &out_off))
      goto error;
  }

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
        if (!gst_mpegh3dasigner_begin_sequence (self, out, out_size, &out_off))
          goto error;
      }

      if (pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME && self->timestamp) {
        emit_size = gst_mpegh3dasigner_insert_timestamp (self,
            out + out_off, out_size - out_off);
        if (emit_size == 0) {
          GST_ERROR_OBJECT (self, "failed to insert TIMESTAMP");
          goto error;
        }
        out_off += emit_size;
      }

      if (pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME && self->au_counter) {
        emit_size = gst_mpegh3dasigner_insert_au_counter (self,
            out + out_off, out_size - out_off, self->frame_count);
        if (emit_size == 0) {
          GST_ERROR_OBJECT (self, "failed to emit AU_COUNTER");
          goto error;
        }
        out_off += emit_size;
      }

      gst_mpegh3da_crypto_update (self->hash, pkt_start, pkt_len);
    }

    memcpy (out + out_off, pkt_start, pkt_len);
    out_off += pkt_len;

    if (self->have_seq_label && pkt->label == self->seq_label
        && pkt->type == GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME) {
      self->frame_count++;
      if (self->frame_count >= self->auth_sequence_length) {
        /* Temporarily defer AUTH_SIG to the start of the next buffer
         * (decoder-behaviour workaround test). The hash is finalized there. */
        self->pending_auth_sig = TRUE;
      }
    }
  }

  gst_buffer_unmap (outbuf, &outmap);
  out_mapped = FALSE;
  gst_buffer_resize (outbuf, 0, out_off);

  /* Log the exact emitted packet order for this output buffer. */
  {
    GstMapInfo fmap;

    if (gst_buffer_map (outbuf, &fmap, GST_MAP_READ)) {
      gst_mpegh3da_mhas_log_buffer (GST_OBJECT (trans), fmap.data, fmap.size,
          "output");
      gst_buffer_unmap (outbuf, &fmap);
    }
  }

  gst_buffer_unmap (inbuf, &inmap);
  in_mapped = FALSE;

done:
  g_array_free (pkts, TRUE);
  return ret;

error:
  if (out_mapped)
    gst_buffer_unmap (outbuf, &outmap);
  if (in_mapped)
    gst_buffer_unmap (inbuf, &inmap);
  ret = GST_FLOW_ERROR;
  goto done;
}

static gboolean
gst_mpegh3dasigner_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (self->hash != NULL) {
        if (self->frame_count > 0) {
          GstBuffer *sig;
          GstMapInfo map;
          gsize off = 0;

          sig = gst_buffer_new_allocate (NULL,
              GST_MPEGH3DA_MAX_AUTH_SIG_SEGMENTS *
              GST_MPEGH3DA_AUTH_SIG_MAX_PACKET_BYTES, NULL);

          if (!gst_buffer_map (sig, &map, GST_MAP_WRITE)) {
            gst_buffer_unref (sig);
            return FALSE;
          }

          if (!gst_mpegh3dasigner_finish_sequence (self, map.data, map.size,
                  &off)) {
            gst_buffer_unmap (sig, &map);
            gst_buffer_unref (sig);
            return FALSE;
          }
          gst_buffer_unmap (sig, &map);
          gst_buffer_resize (sig, 0, off);

          if (gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (trans), sig) !=
              GST_FLOW_OK)
            return FALSE;
        } else {
          g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
          self->sequence_finished = TRUE;
          self->pending_auth_sig = FALSE;
        }
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
      self->frame_count = 0;
      self->have_seq_label = FALSE;
      self->seq_label = 0;
      self->sequence_finished = TRUE;
      self->pending_auth_sig = FALSE;
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
    if (!gst_mpegh3da_parse_uuid (self->content_uuid, self->uuid)) {
      GST_ERROR_OBJECT (self, "invalid content-uuid \"%s\"",
          self->content_uuid);
      return FALSE;
    }
    self->uuid_set = TRUE;
  } else {
    self->uuid_set = FALSE;
  }

  /* Load the private key and pick the signing mode. */
  g_clear_pointer (&self->private_key, EVP_PKEY_free);

  if (self->private_key_path != NULL && *self->private_key_path != '\0') {
    gsize max_sig;

    if (self->public_key_uri == NULL || *self->public_key_uri == '\0') {
      GST_ERROR_OBJECT (self, "public-key-uri is required when "
          "private-key-path is set");
      return FALSE;
    }

    self->private_key =
        gst_mpegh3da_crypto_load_private_key (GST_OBJECT (self),
        self->private_key_path);
    if (self->private_key == NULL)
      return FALSE;

    max_sig = GST_MPEGH3DA_MAX_AUTH_SIG_SEGMENTS *
        GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES;
    if (gst_mpegh3da_crypto_signature_size (self->private_key) > max_sig) {
      GST_ERROR_OBJECT (self, "private key too large (max %" G_GSIZE_FORMAT
          " bytes)", max_sig);
      return FALSE;
    }

    self->rsa_mode = TRUE;
  } else {
    self->rsa_mode = FALSE;
  }

  /* Verify the hash method is supported before streaming. The actual hasher
   * is created per-sequence in gst_mpegh3dasigner_transform(). */
  {
    GstMpegh3daCryptoCtx *probe = gst_mpegh3da_crypto_new (GST_OBJECT (self),
        GST_MPEGH3DA_CRYPTO_MODE_DIGEST, NULL, self->hash_method);

    if (!probe) {
      GST_ERROR_OBJECT (self, "failed to create hasher for hash-method %d",
          self->hash_method);
      return FALSE;
    }
    gst_mpegh3da_crypto_ctx_free (probe);
  }

  self->hash = NULL;
  self->frame_count = 0;
  self->have_seq_label = FALSE;
  self->seq_label = 0;
  self->sequence_finished = TRUE;
  self->pending_auth_sig = FALSE;
  self->buffer_count = 0;

  return TRUE;
}

static gboolean
gst_mpegh3dasigner_stop (GstBaseTransform * trans)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (trans);

  g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
  g_clear_pointer (&self->private_key, EVP_PKEY_free);

  return TRUE;
}

static void
gst_mpegh3dasigner_finalize (GObject * object)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (object);

  g_clear_pointer (&self->content_uuid, g_free);
  g_clear_pointer (&self->private_key_path, g_free);
  g_clear_pointer (&self->public_key_uri, g_free);
  g_clear_pointer (&self->private_key, EVP_PKEY_free);

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
    case PROP_PRIVATE_KEY_PATH:
      g_free (self->private_key_path);
      self->private_key_path = g_value_dup_string (value);
      break;
    case PROP_PUBLIC_KEY_URI:
      g_free (self->public_key_uri);
      self->public_key_uri = g_value_dup_string (value);
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
    case PROP_PRIVATE_KEY_PATH:
      g_value_set_string (value, self->private_key_path);
      break;
    case PROP_PUBLIC_KEY_URI:
      g_value_set_string (value, self->public_key_uri);
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
          "Emit PACTYP_AUTH_TIMESTAMP (authTimeLong) before each frame",
          DEFAULT_TIMESTAMP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONTENT_UUID,
      g_param_spec_string ("content-uuid", "Content UUID",
          "RFC 9562 UUID of the related (sub-)stream (canonical hyphenated "
          "form); empty disables the uuid field in the hash input", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PRIVATE_KEY_PATH,
      g_param_spec_string ("private-key-path", "Private key path",
          "Path to the PEM RSA private key (PKCS#8 or PKCS#1); empty disables "
          "RSA signing and falls back to digest-only mode", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PUBLIC_KEY_URI,
      g_param_spec_string ("public-key-uri", "Public key URI",
          "URI identifying the public key/certificate, emitted as "
          "authSourceURI (authProvID=0)", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "MPEG-H 3D Audio Media Authenticity Signer", "Filter/Audio",
      "Signs MPEG-H 3D audio (MHAS) streams with media authenticity",
      "Fluendo");

  transform_class->transform = gst_mpegh3dasigner_transform;
  transform_class->transform_size = gst_mpegh3dasigner_transform_size;
  transform_class->sink_event = gst_mpegh3dasigner_sink_event;
  transform_class->start = gst_mpegh3dasigner_start;
  transform_class->stop = gst_mpegh3dasigner_stop;
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
  self->private_key_path = NULL;
  self->public_key_uri = NULL;
  self->private_key = NULL;
  self->rsa_mode = FALSE;

  /* In-band byte insertion grows the buffers, so this is a non-in-place
   * transform with a content-dependent output size (transform_size). */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), FALSE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

/* ---- helpers ---- */
/* Start a new authentication sequence: create the hasher, reset the counters
 * and emit AUTH_START (+ UUID). Returns FALSE on failure. */
static gboolean
gst_mpegh3dasigner_begin_sequence (GstMpegh3DASigner * self,
    guint8 * out, gsize out_size, gsize * out_off)
{
  gsize emit_size;

  if (self->rsa_mode) {
    self->hash = gst_mpegh3da_crypto_new (GST_OBJECT (self),
        GST_MPEGH3DA_CRYPTO_MODE_SIGN, self->private_key, self->hash_method);
  } else {
    self->hash = gst_mpegh3da_crypto_new (GST_OBJECT (self),
        GST_MPEGH3DA_CRYPTO_MODE_DIGEST, NULL, self->hash_method);
  }
  if (!self->hash) {
    GST_ERROR_OBJECT (self, "failed to create hasher for hash-method %d",
        self->hash_method);
    return FALSE;
  }

  self->frame_count = 0;
  self->sequence_finished = FALSE;

  emit_size = gst_mpegh3dasigner_insert_auth_start (self,
      out + *out_off, out_size - *out_off);
  if (emit_size == 0) {
    GST_ERROR_OBJECT (self, "failed to emit AUTH_START");
    g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
    self->frame_count = 0;
    self->sequence_finished = TRUE;
    return FALSE;
  }
  *out_off += emit_size;

  if (self->uuid_set) {
    emit_size = gst_mpegh3dasigner_insert_uuid (self,
        out + *out_off, out_size - *out_off);
    if (emit_size == 0) {
      GST_ERROR_OBJECT (self, "failed to emit UUID");
      g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
      self->frame_count = 0;
      self->sequence_finished = TRUE;
      return FALSE;
    }
    *out_off += emit_size;
  }

  return TRUE;
}

/* Finalize the current sequence: insert AUTH_SIG and clear the pending flag.
 * Returns FALSE on failure. */
static gboolean
gst_mpegh3dasigner_finish_sequence (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size, gsize * out_off)
{
  gsize emit_size;

  emit_size = gst_mpegh3dasigner_insert_auth_sig (self,
      out + *out_off, out_size - *out_off);
  if (emit_size == 0) {
    GST_ERROR_OBJECT (self, "failed to insert AUTH_SIG");
    return FALSE;
  }
  *out_off += emit_size;
  self->pending_auth_sig = FALSE;

  return TRUE;
}

/* Emit PACTYP_AUTH_START at the start of a new authentication sequence. It is
 * part of gad_bytes, so it is hashed here. Returns bytes written, or 0. */
static gsize
gst_mpegh3dasigner_insert_auth_start (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size)
{
  GstMpegh3daAuthStart cfg = { 0, };
  guint8 payload[GST_MPEGH3DA_AUTH_START_MAX_PAYLOAD_BYTES];
  gsize plen, tlen;

  cfg.authID = self->auth_id;
  cfg.authHashType = self->hash_method;
  cfg.authKeyID = 0;
  if (self->rsa_mode) {
    gsize uri_len = strlen (self->public_key_uri);

    if (uri_len == 0 || uri_len > GST_MPEGH3DA_AUTH_SOURCE_URI_MAX_BYTES) {
      GST_ERROR_OBJECT (self, "invalid public-key-uri length");
      return 0;
    }
    cfg.authProvID = 0;
    cfg.authSourceURILengthMinus1 = uri_len - 1;
    memcpy (cfg.authSourceURI, self->public_key_uri, uri_len);
  } else {
    cfg.authProvID = 1;
  }
  cfg.authResilienceLevelMax = 0;
  cfg.hasAuthSequenceAUCounter = self->au_counter;
  cfg.isFirstSequence = 1;

  plen = gst_mpegh3da_build_AuthStart (&cfg, payload, sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (out, out_size,
      GST_MPEGH3DA_PACTYP_AUTH_START, self->seq_label, payload, plen);
  if (tlen == 0)
    return 0;

  gst_mpegh3da_crypto_update (self->hash, out, tlen);

  return tlen;
}

/* Emit PACTYP_UUID after AUTH_START and hash it (it is part of gad_bytes).
 * Returns bytes written, or 0 on failure. */
static gsize
gst_mpegh3dasigner_insert_uuid (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size)
{
  GstMpegh3daAuthUUID cfg = { 0, };
  guint8 payload[GST_MPEGH3DA_UUID_MAX_PAYLOAD_BYTES];
  gsize plen, tlen;

  cfg.uuidSegmentStart = 1;
  cfg.uuidSegmentStop = 1;
  cfg.uuidSegmentLengthMinus1 = 15;
  memcpy (cfg.uuid, self->uuid, 16);

  plen = gst_mpegh3da_build_AuthUUID (&cfg, payload, sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (out, out_size,
      GST_MPEGH3DA_PACTYP_UUID, self->seq_label, payload, plen);
  if (tlen == 0)
    return 0;

  gst_mpegh3da_crypto_update (self->hash, out, tlen);

  return tlen;
}

/* Emit PACTYP_TIMESTAMP (authTimeLong) before a frame, so authTime +
 * authTimeOffset records that frame's first-sample time. It is part of
 * gad_bytes, so it is hashed here. Returns bytes written, or 0. */
static gsize
gst_mpegh3dasigner_insert_timestamp (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size)
{
  GstMpegh3daAuthTimestamp cfg = { 0, };
  guint8 payload[GST_MPEGH3DA_TIMESTAMP_MAX_PAYLOAD_BYTES];
  guint64 real_usec = g_get_real_time ();
  gsize plen, tlen;

  cfg.authID = self->auth_id;
  cfg.authTimeType = 0;
  cfg.authTimeOffsetType = 0;
  cfg.authTime = real_usec / G_USEC_PER_SEC - GST_MPEGH3DA_AUTH_TIME_EPOCH_SEC;
  cfg.authTimeOffset = (guint16) ((real_usec / 1000) % 1000);

  plen = gst_mpegh3da_build_AuthTimestamp (&cfg, payload, sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (out, out_size,
      GST_MPEGH3DA_PACTYP_TIMESTAMP, self->seq_label, payload, plen);
  if (tlen == 0)
    return 0;

  gst_mpegh3da_crypto_update (self->hash, out, tlen);

  return tlen;
}

/* Emit PACTYP_AUTH_SEQUENCE_AU_COUNTER before a frame and hash it (it is part
 * of gad_bytes). Returns bytes written, or 0 on failure. */
static gsize
gst_mpegh3dasigner_insert_au_counter (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size, guint64 counter)
{
  GstMpegh3daAuthSequenceAUCounter cfg = { 0, };
  guint8 payload[GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PAYLOAD_BYTES];
  gsize plen, tlen;

  cfg.authID = self->auth_id;
  cfg.authSequenceAUCounter = counter;

  plen = gst_mpegh3da_build_AuthSequenceAUCounter (&cfg, payload,
      sizeof (payload));
  tlen = gst_mpegh3da_mhas_write_packet (out, out_size,
      GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER, self->seq_label, payload,
      plen);
  if (tlen == 0)
    return 0;

  gst_mpegh3da_crypto_update (self->hash, out, tlen);

  return tlen;
}

/* Finalize the current sequence: append the uuid field (gad_bytes || uuid),
 * compute the signature and emit PACTYP_AUTH_SIG after the last frame.
 * RSA mode emits one segmented AUTH_SIG packet per 128-byte chunk; digest-only
 * mode emits a single complete AUTH_SIG. AUTH_SIG is not part of gad_bytes.
 * Returns bytes written, or 0 on failure. */
static gsize
gst_mpegh3dasigner_insert_auth_sig (GstMpegh3DASigner * self, guint8 * out,
    gsize out_size)
{
  GstMpegh3daAuthSig cfg = { 0, };
  guint8 payload[GST_MPEGH3DA_AUTH_SIG_MAX_PAYLOAD_BYTES];
  gsize plen, tlen, total = 0;

  if (!self->hash) {
    self->sequence_finished = TRUE;
    return 0;
  }

  if (self->uuid_set)
    gst_mpegh3da_crypto_update (self->hash, self->uuid, 16);

  if (self->rsa_mode) {
    guint8 sig[GST_MPEGH3DA_MAX_AUTH_SIG_SEGMENTS *
        GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES];
    gsize sig_len = sizeof (sig);
    gsize off = 0;
    guint n_segments, seg;

    if (!gst_mpegh3da_crypto_sign_final (self->hash, sig, &sig_len))
      goto error;

    n_segments = (sig_len + GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES - 1) /
        GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES;

    cfg.authID = self->auth_id;
    cfg.authPartialSig = 1;
    cfg.authABREnable = 0;

    for (seg = 0; seg < n_segments; seg++) {
      gsize seg_len = MIN (sig_len - off, GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES);

      cfg.sigSegmentStart = (seg == 0);
      cfg.sigSegmentStop = (seg == n_segments - 1);
      cfg.sigSegmentLengthMinus1 = (guint8) (seg_len - 1);
      memcpy (cfg.sigPartial, sig + off, seg_len);

      plen = gst_mpegh3da_build_AuthSig (&cfg, payload, sizeof (payload));
      tlen = gst_mpegh3da_mhas_write_packet (out + total, out_size - total,
          GST_MPEGH3DA_PACTYP_AUTH_SIG, self->seq_label, payload, plen);
      if (tlen == 0)
        goto error;
      total += tlen;
      off += seg_len;
    }
  } else {
    gsize digest_len = gst_mpegh3da_crypto_digest_size (self->hash);

    gst_mpegh3da_crypto_digest_final (self->hash, cfg.sigComplete, &digest_len);
    gst_mpegh3da_crypto_log_digest (GST_OBJECT (self), cfg.sigComplete,
        digest_len);

    cfg.authID = self->auth_id;
    cfg.sigLengthMinus1 = (guint8) (digest_len - 1);

    plen = gst_mpegh3da_build_AuthSig (&cfg, payload, sizeof (payload));
    tlen = gst_mpegh3da_mhas_write_packet (out, out_size,
        GST_MPEGH3DA_PACTYP_AUTH_SIG, self->seq_label, payload, plen);
    if (tlen == 0)
      goto error;
    total = tlen;
  }

  g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
  self->frame_count = 0;
  self->sequence_finished = TRUE;

  return total;

error:
  g_clear_pointer (&self->hash, gst_mpegh3da_crypto_ctx_free);
  self->frame_count = 0;
  self->sequence_finished = TRUE;
  return 0;
}
