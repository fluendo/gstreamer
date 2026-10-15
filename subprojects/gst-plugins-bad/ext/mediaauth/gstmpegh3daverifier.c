/* TODO: add copyright and license header */

#include <string.h>

#include "gstmpegh3daverifier.h"
#include "gstmpegh3daparse.h"
#include "gstmpegh3daverify.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

#define DEFAULT_FAIL_ON_VERIFICATION_ERROR FALSE

enum
{
  PROP_0,
  PROP_FAIL_ON_VERIFICATION_ERROR,
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

#define parent_class gst_mpegh3daverifier_parent_class
G_DEFINE_TYPE (GstMpegh3DAVerifier, gst_mpegh3daverifier, GST_TYPE_ELEMENT);

/* ---- element vfuncs ---- */
static void gst_mpegh3daverifier_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mpegh3daverifier_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mpegh3daverifier_finalize (GObject * object);
static GstFlowReturn gst_mpegh3daverifier_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_mpegh3daverifier_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

/* ---- helpers ---- */
static void gst_mpegh3daverifier_post_result (GstMpegh3DAVerifier * self,
    gboolean verified, gsize digest_size, const gchar * error);
static GstFlowReturn gst_mpegh3daverifier_finish_sequence (GstMpegh3DAVerifier *
    self);
static GstFlowReturn gst_mpegh3daverifier_process_buffer (GstMpegh3DAVerifier *
    self, const guint8 * data, gsize size);

/* ---- element vfuncs ---- */

static GstFlowReturn
gst_mpegh3daverifier_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMpegh3DAVerifier *self = GST_MPEGH3DAVERIFIER (parent);
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_OK;

  self->buffer_count++;
  GST_INFO_OBJECT (self, "=== buffer %" G_GUINT64_FORMAT " (%"
      G_GSIZE_FORMAT " bytes) ===", self->buffer_count,
      gst_buffer_get_size (buffer));

  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    ret = gst_mpegh3daverifier_process_buffer (self, map.data, map.size);
    gst_buffer_unmap (buffer, &map);
  }

  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (buffer);
    return ret;
  }

  return gst_pad_push (self->srcpad, buffer);
}

static gboolean
gst_mpegh3daverifier_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMpegh3DAVerifier *self = GST_MPEGH3DAVERIFIER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (self->in_sequence) {
        GST_WARNING_OBJECT (self, "EOS in the middle of an authentication "
            "sequence, discarding");
        gst_mpegh3da_auth_data_clear (&self->auth_data);
        self->in_sequence = FALSE;
        self->seq_label = 0;
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_mpegh3da_auth_data_clear (&self->auth_data);
      self->in_sequence = FALSE;
      self->seq_label = 0;
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static void
gst_mpegh3daverifier_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpegh3DAVerifier *self = GST_MPEGH3DAVERIFIER (object);

  switch (prop_id) {
    case PROP_FAIL_ON_VERIFICATION_ERROR:
      self->fail_on_verification_error = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegh3daverifier_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpegh3DAVerifier *self = GST_MPEGH3DAVERIFIER (object);

  switch (prop_id) {
    case PROP_FAIL_ON_VERIFICATION_ERROR:
      g_value_set_boolean (value, self->fail_on_verification_error);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegh3daverifier_class_init (GstMpegh3DAVerifierClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_mpegh3daverifier_set_property;
  gobject_class->get_property = gst_mpegh3daverifier_get_property;
  gobject_class->finalize = gst_mpegh3daverifier_finalize;

  g_object_class_install_property (gobject_class,
      PROP_FAIL_ON_VERIFICATION_ERROR,
      g_param_spec_boolean ("fail-on-verification-error",
          "Fail on verification error",
          "Fail the pipeline when a digest mismatch is detected",
          DEFAULT_FAIL_ON_VERIFICATION_ERROR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "MPEG-H 3D Audio Media Authenticity Verifier", "Filter/Audio",
      "Verifies MPEG-H 3D audio (MHAS) media authenticity", "Fluendo");
}

static void
gst_mpegh3daverifier_init (GstMpegh3DAVerifier * self)
{
  GstPadTemplate *templ;

  self->fail_on_verification_error = DEFAULT_FAIL_ON_VERIFICATION_ERROR;

  gst_mpegh3da_auth_data_init (&self->auth_data);
  self->in_sequence = FALSE;
  self->seq_label = 0;

  templ = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self),
      "sink");
  self->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_chain_function (self->sinkpad, gst_mpegh3daverifier_chain);
  gst_pad_set_event_function (self->sinkpad, gst_mpegh3daverifier_sink_event);
  /* Forward the sticky CAPS event downstream (like dscverifier's PROXY_CAPS),
   * otherwise the decoder never receives caps and stays uninitialized. */
  GST_PAD_SET_PROXY_CAPS (self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  templ = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self),
      "src");
  self->srcpad = gst_pad_new_from_template (templ, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->buffer_count = 0;
}

static void
gst_mpegh3daverifier_finalize (GObject * object)
{
  GstMpegh3DAVerifier *self = GST_MPEGH3DAVERIFIER (object);

  gst_mpegh3da_auth_data_clear (&self->auth_data);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* ---- helpers ---- */

/* Post the per-sequence verification result (dsc-verification-result style,
 * without the certificate fields). */
static void
gst_mpegh3daverifier_post_result (GstMpegh3DAVerifier * self,
    gboolean verified, gsize digest_size, const gchar * error)
{
  GstStructure *s;
  GstMessage *msg;

  s = gst_structure_new ("mpegh3da-verification-result",
      "verified", G_TYPE_BOOLEAN, verified,
      "digest-size", G_TYPE_UINT, (guint) digest_size, NULL);
  if (error != NULL)
    gst_structure_set (s, "error", G_TYPE_STRING, error, NULL);

  msg = gst_message_new_element (GST_OBJECT_CAST (self), s);
  gst_element_post_message (GST_ELEMENT_CAST (self), msg);
}

/* Finalize the current sequence: finish the digest (appending the uuid field),
 * compare it with the received sigComplete, post the result and reset state. */
static GstFlowReturn
gst_mpegh3daverifier_finish_sequence (GstMpegh3DAVerifier * self)
{
  guint8 digest[GST_MPEGH3DA_HASH_MAX_DIGEST_BYTES];
  gsize digest_size = 0;
  gboolean verified = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_mpegh3da_auth_data_hash_finish (&self->auth_data, digest,
          &digest_size)) {
    gst_mpegh3daverifier_post_result (self, FALSE, 0, "hash finalize failed");
  } else {
    gst_mpegh3da_crypto_log_digest (GST_OBJECT (self), digest, digest_size);

    verified = gst_mpegh3da_auth_data_verify_signature (GST_OBJECT (self),
        &self->auth_data, digest, digest_size);

    if (verified)
      gst_mpegh3daverifier_post_result (self, TRUE, digest_size, NULL);
    else
      gst_mpegh3daverifier_post_result (self, FALSE, digest_size,
          "digest mismatch");
  }

  gst_mpegh3da_auth_data_log (GST_OBJECT (self), &self->auth_data);

  gst_mpegh3da_auth_data_clear (&self->auth_data);
  self->in_sequence = FALSE;
  self->seq_label = 0;

  if (!verified && self->fail_on_verification_error)
    ret = GST_FLOW_ERROR;

  return ret;
}

/* Walk the MHAS packets of a buffer and drive the verification state machine
 * (§17.12.4). Returns GST_FLOW_ERROR on malformed input or, when
 * fail-on-verification-error is set, on a digest mismatch. */
static GstFlowReturn
gst_mpegh3daverifier_process_buffer (GstMpegh3DAVerifier * self,
    const guint8 * data, gsize size)
{
  GstBitReader br;

  gst_bit_reader_init (&br, data, (guint) size);
  while (gst_bit_reader_get_remaining (&br) != 0) {
    GstMpegh3daMhasPacket pkt;

    if (!gst_mpegh3da_mhas_parse_packet (&br, &pkt)) {
      GST_WARNING_OBJECT (self, "malformed MHAS packet, dropping buffer");
      return GST_FLOW_ERROR;
    }

    gst_mpegh3da_mhas_log_packet (GST_OBJECT (self), &pkt);

    if (pkt.type == GST_MPEGH3DA_PACTYP_AUTH_START) {
      if (self->in_sequence) {
        GST_WARNING_OBJECT (self, "AUTH_START while a sequence is active: "
            "overlapping sequences are not supported, resetting");
        gst_mpegh3da_auth_data_clear (&self->auth_data);
      }

      gst_mpegh3da_auth_data_init (&self->auth_data);
      if (!gst_mpegh3da_parse_AuthStart (GST_OBJECT (self), pkt.payload,
              pkt.length, &self->auth_data.auth_start)) {
        GST_WARNING_OBJECT (self, "failed to parse AUTH_START");
        return GST_FLOW_ERROR;
      }

      self->seq_label = pkt.label;
      self->in_sequence = TRUE;

      if (!gst_mpegh3da_auth_data_start_hash (&self->auth_data,
              GST_OBJECT (self))) {
        GST_ERROR_OBJECT (self, "unsupported hash method %u",
            self->auth_data.auth_start.authHashType);
        self->in_sequence = FALSE;
        return GST_FLOW_ERROR;
      }

      /* AUTH_START is part of gad_bytes (§17.12.4.1). */
      gst_mpegh3da_auth_data_hash_update (&self->auth_data,
          pkt.payload - pkt.header_size, pkt.header_size + pkt.length);
      continue;
    }

    /* Skip packets that do not belong to the current sequence: either before
     * it has started, or with a different MHASPacketLabel (unless the sequence
     * label is 0, which covers all sub-streams). */
    if (!self->in_sequence)
      continue;

    if (self->seq_label != 0 && pkt.label != self->seq_label)
      continue;

    switch (pkt.type) {
      case GST_MPEGH3DA_PACTYP_AUTH_SIG:
      {
        GstFlowReturn ret;

        if (!gst_mpegh3da_parse_AuthSig (GST_OBJECT (self), pkt.payload,
                pkt.length, &self->auth_data.auth_sig)) {
          GST_WARNING_OBJECT (self, "failed to parse AUTH_SIG");
          return GST_FLOW_ERROR;
        }

        ret = gst_mpegh3daverifier_finish_sequence (self);
        if (ret != GST_FLOW_OK)
          return ret;
        continue;               /* AUTH_SIG is not part of gad_bytes */
      }
      case GST_MPEGH3DA_PACTYP_UUID:
      {
        if (!gst_mpegh3da_parse_AuthUUID (GST_OBJECT (self), pkt.payload,
                pkt.length, &self->auth_data.auth_uuid)) {
          GST_WARNING_OBJECT (self, "failed to parse UUID");
          return GST_FLOW_ERROR;
        }
        self->auth_data.auth_uuid_present = TRUE;
        break;
      }
      case GST_MPEGH3DA_PACTYP_TIMESTAMP:
      {
        if (!gst_mpegh3da_parse_AuthTimestamp (GST_OBJECT (self), pkt.payload,
                pkt.length, &self->auth_data.auth_timestamp)) {
          GST_WARNING_OBJECT (self, "failed to parse TIMESTAMP");
          return GST_FLOW_ERROR;
        }
        self->auth_data.auth_timestamp_present = TRUE;
        break;
      }
      case GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER:
      {
        GstMpegh3daAuthSequenceAUCounter cfg;

        if (!gst_mpegh3da_parse_AuthSequenceAUCounter (GST_OBJECT (self),
                pkt.payload, pkt.length, &cfg)) {
          GST_WARNING_OBJECT (self, "failed to parse AUTH_SEQUENCE_AU_COUNTER");
          return GST_FLOW_ERROR;
        }
        break;
      }
      default:
        break;
    }

    /* Everything in the sequence except AUTH_SIG and the default-excluded
     * types enters gad_bytes (§17.12.4.1). */
    if (!gst_mpegh3da_mhas_packet_type_is_excluded (pkt.type))
      gst_mpegh3da_auth_data_hash_update (&self->auth_data,
          pkt.payload - pkt.header_size, pkt.header_size + pkt.length);
  }

  return GST_FLOW_OK;
}
