/* TODO: add copyright and license header */

#include "gstmpegh3daverifier.h"
#include "gstmpegh3daparse.h"
#include "gstmpegh3daauthdata.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

#define DEFAULT_FAIL_ON_VERIFICATION_ERROR FALSE

enum
{
  PROP_0,
  PROP_FAIL_ON_VERIFICATION_ERROR,
  PROP_TRUST_STORE_PATH,
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
    gboolean verified, gsize signature_size, const gchar * error);
static gchar *gst_mpegh3daverifier_resolve_cert_path (GstMpegh3DAVerifier *
    self, const GstMpegh3daAuthStart * auth_start);
static EVP_PKEY *gst_mpegh3daverifier_get_public_key (GstMpegh3DAVerifier *
    self, const GstMpegh3daAuthStart * auth_start);
static gboolean gst_mpegh3daverifier_finish_sequence (GstMpegh3DAVerifier *
    self);
static GstFlowReturn gst_mpegh3daverifier_process_buffer (GstMpegh3DAVerifier *
    self, const guint8 * data, gsize size);

/* ---- per-packet-type handlers ---- */
static gboolean gst_mpegh3da_handle_authStart (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt);
static gboolean gst_mpegh3da_handle_authSig (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt);
static gboolean gst_mpegh3da_handle_authUUID (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt);
static gboolean gst_mpegh3da_handle_authTimestamp (GstMpegh3DAVerifier *
    self, const GstMpegh3daMhasPacket * pkt);
static gboolean gst_mpegh3da_handle_authSequenceAUCounter
    (GstMpegh3DAVerifier * self, const GstMpegh3daMhasPacket * pkt);

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
  } else {
    GST_WARNING_OBJECT (self, "failed to map buffer, passing it through "
        "unverified");
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
      self->cert_status = GST_MPEGH3DA_CERT_UNVERIFIED;
      /* Drop cached keys/statuses on flush so the next stream reloads its
       * certificates. */
      g_hash_table_remove_all (self->public_key_cache);
      g_hash_table_remove_all (self->cert_verification_cache);
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
    case PROP_TRUST_STORE_PATH:
      g_free (self->trust_store_path);
      self->trust_store_path = g_value_dup_string (value);
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
    case PROP_TRUST_STORE_PATH:
      g_value_set_string (value, self->trust_store_path);
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
          "Fail the pipeline when a verification failure is detected",
          DEFAULT_FAIL_ON_VERIFICATION_ERROR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRUST_STORE_PATH,
      g_param_spec_string ("trust-store-path", "Trust store path",
          "Directory with trusted CA certificates are stored. empty "
          "disables certificate trust validation", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "MPEG-H 3D Audio Media Authenticity Verifier", "Filter/Audio",
      "Verifies MPEG-H 3D audio (MHAS) media authenticity", "Fluendo");
}

/* GHashTable value destructor for cached EVP_PKEYs. */
static void
public_key_free (gpointer pkey)
{
  EVP_PKEY_free ((EVP_PKEY *) pkey);
}

static void
gst_mpegh3daverifier_init (GstMpegh3DAVerifier * self)
{
  GstPadTemplate *templ;

  self->fail_on_verification_error = DEFAULT_FAIL_ON_VERIFICATION_ERROR;
  self->trust_store_path = NULL;
  self->cert_status = GST_MPEGH3DA_CERT_UNVERIFIED;
  self->verification_failed = FALSE;

  self->public_key_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, public_key_free);
  self->cert_verification_cache = g_hash_table_new_full (g_str_hash,
      g_str_equal, g_free, NULL);

  gst_mpegh3da_auth_data_init (&self->auth_data);
  self->in_sequence = FALSE;
  self->seq_label = 0;

  templ = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (self),
      "sink");
  self->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_chain_function (self->sinkpad, gst_mpegh3daverifier_chain);
  gst_pad_set_event_function (self->sinkpad, gst_mpegh3daverifier_sink_event);
  /* Forward the sticky CAPS event downstream, otherwise the decoder never
   * receives caps and stays uninitialized. */
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
  g_clear_pointer (&self->trust_store_path, g_free);
  g_clear_pointer (&self->public_key_cache, g_hash_table_destroy);
  g_clear_pointer (&self->cert_verification_cache, g_hash_table_destroy);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* ---- helpers ---- */

/* Post the per-sequence verification result. The certificate fields are only
 * meaningful for RSA (authProvID == 0) sequences; digest-only sequences carry
 * no certificate. Verification failures are reported through a warning and
 * the posted element message; the caller may additionally propagate a flow
 * error when fail-on-verification-error is enabled. */
static void
gst_mpegh3daverifier_post_result (GstMpegh3DAVerifier * self,
    gboolean verified, gsize signature_size, const gchar * error)
{
  GstStructure *s;
  GstMessage *msg;

  if (verified)
    GST_INFO_OBJECT (self, "media authenticity verification succeeded");
  else
    GST_WARNING_OBJECT (self, "media authenticity verification failed%s%s",
        error != NULL ? ": " : "", error != NULL ? error : "");

  s = gst_structure_new ("mpegh3da-verification-result",
      "verified", G_TYPE_BOOLEAN, verified,
      "signature-size", G_TYPE_UINT, (guint) signature_size, NULL);

  if (self->auth_data.rsa_mode) {
    gst_structure_set (s,
        "certificate-trusted", G_TYPE_BOOLEAN,
        self->cert_status == GST_MPEGH3DA_CERT_VERIFIED,
        "certificate-status", G_TYPE_STRING,
        gst_mpegh3da_cert_status_name (self->cert_status), NULL);
  }

  if (self->auth_data.auth_uuid_present) {
    gchar uuid_str[37];
    const guint8 *u = self->auth_data.auth_uuid.uuid;

    /* RFC 9562 canonical hyphenated form. */
    g_snprintf (uuid_str, sizeof (uuid_str),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
        u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    gst_structure_set (s, "content-uuid", G_TYPE_STRING, uuid_str, NULL);
  }

  if (self->auth_data.auth_start.hasAuthSequenceAUCounter)
    gst_structure_set (s, "auth-sequence-au-counters-valid", G_TYPE_BOOLEAN,
        self->auth_data.auth_sequence_au_counters_valid, NULL);

  if (error != NULL)
    gst_structure_set (s, "error", G_TYPE_STRING, error, NULL);

  msg = gst_message_new_element (GST_OBJECT_CAST (self), s);
  gst_element_post_message (GST_ELEMENT_CAST (self), msg);
}

/* Resolve the certificate file from the authSourceURI carried in
 * mpegh3daAuthStart(). file:// URIs are converted to local paths; any other
 * URI (or a plain path) is used as-is. */
static gchar *
gst_mpegh3daverifier_resolve_cert_path (GstMpegh3DAVerifier * self,
    const GstMpegh3daAuthStart * auth_start)
{
  gchar *uri;
  gchar *path = NULL;
  gsize uri_len = (gsize) auth_start->authSourceURILengthMinus1 + 1;
  GUri *parsed;
  GError *error = NULL;
  const gchar *scheme;

  uri = g_strndup ((const gchar *) auth_start->authSourceURI, uri_len);

  parsed = g_uri_parse (uri, G_URI_FLAGS_NONE, &error);
  if (parsed == NULL) {
    GST_DEBUG_OBJECT (self, "authSourceURI \"%s\" is not an absolute URI "
        "(%s), treating it as a path", uri, error->message);
    g_clear_error (&error);
    return uri;
  }

  scheme = g_uri_get_scheme (parsed);
  if (g_strcmp0 (scheme, "file") == 0) {
    path = g_filename_from_uri (uri, NULL, &error);
    if (path == NULL) {
      GST_WARNING_OBJECT (self, "failed to convert authSourceURI \"%s\" to a "
          "local path: %s", uri, error->message);
      g_clear_error (&error);
    }
  } else if (scheme != NULL) {
    GST_WARNING_OBJECT (self, "unsupported URI scheme \"%s\" in "
        "authSourceURI \"%s\", using it as a local path", scheme, uri);
  }

  g_clear_pointer (&parsed, g_uri_unref);

  if (path == NULL)
    return uri;

  g_free (uri);
  return path;
}

/* Return the public key for @auth_start, using the per-URI caches. On a cache
 * miss the certificate is read, parsed and trust-checked once, then both the
 * key and the resulting status are cached. Returns a new EVP_PKEY reference,
 * or NULL on failure. */
static EVP_PKEY *
gst_mpegh3daverifier_get_public_key (GstMpegh3DAVerifier * self,
    const GstMpegh3daAuthStart * auth_start)
{
  gchar *uri;
  gchar *path;
  EVP_PKEY *public_key;
  gsize uri_len = (gsize) auth_start->authSourceURILengthMinus1 + 1;

  uri = g_strndup ((const gchar *) auth_start->authSourceURI, uri_len);

  public_key = g_hash_table_lookup (self->public_key_cache, uri);
  if (public_key != NULL) {
    gpointer status = g_hash_table_lookup (self->cert_verification_cache, uri);

    self->cert_status = (GstMpegh3daCertStatus) GPOINTER_TO_INT (status);
    EVP_PKEY_up_ref (public_key);

    GST_DEBUG_OBJECT (self, "using cached public key for authSourceURI "
        "\"%s\" (%s)", uri, gst_mpegh3da_cert_status_name (self->cert_status));

    g_free (uri);
    return public_key;
  }

  path = gst_mpegh3daverifier_resolve_cert_path (self, auth_start);

  public_key =
      gst_mpegh3da_crypto_load_public_key_from_cert_checked (GST_OBJECT (self),
      path, self->trust_store_path, &self->cert_status);
  if (public_key == NULL) {
    g_free (path);
    g_free (uri);
    return NULL;
  }

  GST_INFO_OBJECT (self, "loaded certificate \"%s\" for authSourceURI "
      "\"%s\" (%s)", path, uri,
      gst_mpegh3da_cert_status_name (self->cert_status));

  /* The cache owns one reference, the caller gets another. */
  g_hash_table_insert (self->public_key_cache, g_strdup (uri), public_key);
  EVP_PKEY_up_ref (public_key);
  g_hash_table_insert (self->cert_verification_cache, g_strdup (uri),
      GINT_TO_POINTER (self->cert_status));

  g_free (path);
  g_free (uri);

  return public_key;
}

/* Finalize the current sequence: verify the signature (RSA or digest), post
 * the result and reset state. Returns FALSE when the sequence did not verify
 * or its certificate is untrusted; the caller decides whether that stops the
 * pipeline. */
static gboolean
gst_mpegh3daverifier_finish_sequence (GstMpegh3DAVerifier * self)
{
  gboolean verified = FALSE;
  gboolean authentic;

  if (self->auth_data.rsa_mode) {
    gboolean error = FALSE;

    verified = gst_mpegh3da_auth_data_verify_finish (&self->auth_data,
        GST_OBJECT (self), &error);

    if (error) {
      gst_mpegh3daverifier_post_result (self, FALSE,
          self->auth_data.signature_len, "verification error");
    } else if (verified) {
      gst_mpegh3daverifier_post_result (self, TRUE,
          self->auth_data.signature_len, NULL);

      if (self->cert_status == GST_MPEGH3DA_CERT_UNTRUSTED)
        GST_WARNING_OBJECT (self, "signature is valid but the certificate "
            "is untrusted");
    } else {
      gst_mpegh3daverifier_post_result (self, FALSE,
          self->auth_data.signature_len, "signature mismatch");
    }
  } else {
    guint8 digest[GST_MPEGH3DA_HASH_MAX_DIGEST_BYTES];
    gsize digest_size = 0;

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
  }

  /* An untrusted certificate invalidates an otherwise valid signature. */
  authentic = verified && (self->auth_data.rsa_mode ?
      self->cert_status != GST_MPEGH3DA_CERT_UNTRUSTED : TRUE);

  gst_mpegh3da_auth_data_log (GST_OBJECT (self), &self->auth_data);

  gst_mpegh3da_auth_data_clear (&self->auth_data);
  self->in_sequence = FALSE;
  self->seq_label = 0;

  return authentic;
}

/* Walk the MHAS packets of a buffer and drive the verification state machine
 * (§17.12.4). Malformed packets and handler failures are reported as warnings
 * and never interrupt the data flow; verification failures only do so when
 * fail-on-verification-error is enabled. */
static GstFlowReturn
gst_mpegh3daverifier_process_buffer (GstMpegh3DAVerifier * self,
    const guint8 * data, gsize size)
{
  GstBitReader br;

  gst_bit_reader_init (&br, data, (guint) size);
  while (gst_bit_reader_get_remaining (&br) != 0) {
    GstMpegh3daMhasPacket pkt;
    gboolean result;

    if (!gst_mpegh3da_mhas_parse_packet (&br, &pkt)) {
      GST_WARNING_OBJECT (self, "malformed MHAS packet, stopping "
          "verification for this buffer");
      break;
    }

    gst_mpegh3da_mhas_log_packet (GST_OBJECT (self), &pkt);

    /* AUTH_START opens a sequence, so it is handled before the sequence
     * membership checks below. */
    if (pkt.type == GST_MPEGH3DA_PACTYP_AUTH_START) {
      if (!gst_mpegh3da_handle_authStart (self, &pkt))
        GST_WARNING_OBJECT (self, "failed to handle %s packet, continuing",
            gst_mpegh3da_mhas_packet_type_name (pkt.type));
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
        result = gst_mpegh3da_handle_authSig (self, &pkt);
        if (!result)
          GST_WARNING_OBJECT (self, "failed to handle %s packet, continuing",
              gst_mpegh3da_mhas_packet_type_name (pkt.type));

        if (self->verification_failed) {
          self->verification_failed = FALSE;
          if (self->fail_on_verification_error) {
            GST_ERROR_OBJECT (self, "verification failed, stopping "
                "pipeline");
            return GST_FLOW_ERROR;
          }
        }
        continue;               /* AUTH_SIG is not part of gad_bytes */
      case GST_MPEGH3DA_PACTYP_UUID:
        result = gst_mpegh3da_handle_authUUID (self, &pkt);
        break;
      case GST_MPEGH3DA_PACTYP_TIMESTAMP:
        result = gst_mpegh3da_handle_authTimestamp (self, &pkt);
        break;
      case GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER:
        result = gst_mpegh3da_handle_authSequenceAUCounter (self, &pkt);
        break;
      default:
        result = TRUE;
        break;
    }

    if (!result)
      GST_WARNING_OBJECT (self, "failed to handle %s packet, continuing",
          gst_mpegh3da_mhas_packet_type_name (pkt.type));

    /* Everything in the sequence except AUTH_SIG and the default-excluded
     * types enters gad_bytes (§17.12.4.1). A packet whose handler failed is
     * still hashed: the signer covered its raw bytes too. */
    if (!gst_mpegh3da_mhas_packet_type_is_excluded (pkt.type))
      gst_mpegh3da_auth_data_hash_update (&self->auth_data,
          pkt.payload - pkt.header_size, pkt.header_size + pkt.length);
  }

  return GST_FLOW_OK;
}

/* ---- per-packet-type handlers ---- */

/* mpegh3daAuthStart(): opens a new authentication sequence and starts the
 * signature verification or hashing context. The packet itself is part of
 * gad_bytes (§17.12.4.1). Failures are logged at DEBUG level and reported by
 * the caller; a failed AUTH_START cannot leave a sequence open. */
static gboolean
gst_mpegh3da_handle_authStart (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt)
{
  if (self->in_sequence) {
    GST_DEBUG_OBJECT (self, "AUTH_START while a sequence is active: "
        "overlapping sequences are not supported, resetting");
    gst_mpegh3da_auth_data_clear (&self->auth_data);
  }

  gst_mpegh3da_auth_data_init (&self->auth_data);
  if (!gst_mpegh3da_parse_AuthStart (GST_OBJECT (self), pkt->payload,
          pkt->length, &self->auth_data.auth_start)) {
    GST_DEBUG_OBJECT (self, "failed to parse AUTH_START, ignoring sequence");
    gst_mpegh3daverifier_post_result (self, FALSE, 0,
        "failed to parse AUTH_START");
    gst_mpegh3da_auth_data_clear (&self->auth_data);
    self->in_sequence = FALSE;
    self->seq_label = 0;
    return FALSE;
  }

  self->seq_label = pkt->label;
  self->in_sequence = TRUE;
  self->cert_status = GST_MPEGH3DA_CERT_UNVERIFIED;

  if (self->auth_data.auth_start.authProvID == 0) {
    EVP_PKEY *public_key;

    public_key = gst_mpegh3daverifier_get_public_key (self,
        &self->auth_data.auth_start);
    if (public_key == NULL) {
      GST_DEBUG_OBJECT (self, "failed to load the public key for "
          "authSourceURI, ignoring sequence");
      gst_mpegh3daverifier_post_result (self, FALSE, 0,
          "failed to load public key");
      gst_mpegh3da_auth_data_clear (&self->auth_data);
      self->in_sequence = FALSE;
      self->seq_label = 0;
      return FALSE;
    }

    if (!gst_mpegh3da_auth_data_start_verify (&self->auth_data,
            GST_OBJECT (self), public_key)) {
      GST_DEBUG_OBJECT (self, "failed to start verification, ignoring "
          "sequence");
      gst_mpegh3daverifier_post_result (self, FALSE, 0,
          "failed to start verification");
      EVP_PKEY_free (public_key);
      gst_mpegh3da_auth_data_clear (&self->auth_data);
      self->in_sequence = FALSE;
      self->seq_label = 0;
      return FALSE;
    }

    EVP_PKEY_free (public_key);
  } else {
    if (!gst_mpegh3da_auth_data_start_hash (&self->auth_data,
            GST_OBJECT (self))) {
      GST_DEBUG_OBJECT (self, "unsupported hash method %"
          G_GUINT64_FORMAT ", ignoring sequence",
          self->auth_data.auth_start.authHashType);
      gst_mpegh3daverifier_post_result (self, FALSE, 0,
          "unsupported hash method");
      gst_mpegh3da_auth_data_clear (&self->auth_data);
      self->in_sequence = FALSE;
      self->seq_label = 0;
      return FALSE;
    }
  }

  /* AUTH_START is part of gad_bytes (§17.12.4.1). */
  gst_mpegh3da_auth_data_hash_update (&self->auth_data,
      pkt->payload - pkt->header_size, pkt->header_size + pkt->length);

  return TRUE;
}

/* mpegh3daAuthSig(): accumulates a (possibly segmented) signature and closes
 * the sequence once the last segment arrives. AUTH_SIG is not part of
 * gad_bytes. A malformed segment is skipped without discarding the sequence:
 * later segments may still complete it. */
static gboolean
gst_mpegh3da_handle_authSig (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt)
{
  GstMpegh3daAuthSig sig;

  if (!gst_mpegh3da_parse_AuthSig (GST_OBJECT (self), pkt->payload,
          pkt->length, &sig)) {
    GST_DEBUG_OBJECT (self, "failed to parse AUTH_SIG, skipping segment");
    return FALSE;
  }

  self->auth_data.auth_sig = sig;

  if (sig.authPartialSig != 0) {
    if (!gst_mpegh3da_auth_data_append_signature (&self->auth_data,
            sig.sigPartial, (gsize) sig.sigSegmentLengthMinus1 + 1)) {
      GST_DEBUG_OBJECT (self, "signature too large, skipping segment");
      return FALSE;
    }
    /* Only the segment with sigSegmentStop closes the sequence. */
    if (!sig.sigSegmentStop)
      return TRUE;
  }

  self->verification_failed = !gst_mpegh3daverifier_finish_sequence (self);

  return TRUE;
}

/* authUUID(): records the content UUID of the current sequence. */
static gboolean
gst_mpegh3da_handle_authUUID (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt)
{
  if (!gst_mpegh3da_parse_AuthUUID (GST_OBJECT (self), pkt->payload,
          pkt->length, &self->auth_data.auth_uuid)) {
    GST_DEBUG_OBJECT (self, "failed to parse UUID, skipping packet");
    return FALSE;
  }

  self->auth_data.auth_uuid_present = TRUE;

  return TRUE;
}

/* authTimestamp(): records the timestamp of the current sequence. */
static gboolean
gst_mpegh3da_handle_authTimestamp (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt)
{
  if (!gst_mpegh3da_parse_AuthTimestamp (GST_OBJECT (self), pkt->payload,
          pkt->length, &self->auth_data.auth_timestamp)) {
    GST_DEBUG_OBJECT (self, "failed to parse TIMESTAMP, skipping packet");
    return FALSE;
  }

  self->auth_data.auth_timestamp_present = TRUE;

  return TRUE;
}

/* authSequenceAUCounter(): validates the access-unit counter against the
 * sequence configuration. */
static gboolean
gst_mpegh3da_handle_authSequenceAUCounter (GstMpegh3DAVerifier * self,
    const GstMpegh3daMhasPacket * pkt)
{
  GstMpegh3daAuthSequenceAUCounter cfg;

  if (!gst_mpegh3da_parse_AuthSequenceAUCounter (GST_OBJECT (self),
          pkt->payload, pkt->length, &cfg)) {
    GST_DEBUG_OBJECT (self, "failed to parse AUTH_SEQUENCE_AU_COUNTER, "
        "skipping packet");
    return FALSE;
  }

  gst_mpegh3da_auth_data_validate_au_counter (&self->auth_data,
      GST_OBJECT (self), cfg.authSequenceAUCounter);

  return TRUE;
}
