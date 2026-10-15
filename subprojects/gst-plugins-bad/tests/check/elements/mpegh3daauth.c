/* GStreamer
 *
 * MPEG-H 3D Audio media authenticity — black-box tests.
 *
 * These tests drive mpegh3dasigner / mpegh3daverifier through gst_harness with
 * a hand-built MHAS stream and only assert observable behaviour: packet
 * injection, the full "mpegh3da-verification-result" message and tamper
 * detection.
 */

#include <gst/check/check.h>
#include <gst/base/gstbitreader.h>
#include <glib/gstdio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <string.h>

#define MHAS_CAPS \
  "audio/x-mpeg-h, framed=(boolean)true, stream-format=(string)mhas, " \
  "stream-type=(string)single"

#define DEFAULT_UUID "12345678-9abc-def0-1234-56789abcdef0"

typedef enum
{
  TAMPER_NONE = 0,
  TAMPER_FRAME,                 /* flip one byte of the first frame */
  TAMPER_COUNTER,               /* reset the 2nd AU counter to 0 */
} TamperKind;

/* Captured fields of the "mpegh3da-verification-result" message. Each test
 * asserts this whole shape, i.e. a mock of the expected message. */
typedef struct
{
  gboolean flow_error;
  guint n_results;

  gboolean verified;
  guint signature_size;

  gboolean has_cert_trusted;
  gboolean cert_trusted;
  gboolean has_cert_status;
  gchar *cert_status;

  gboolean has_uuid;
  gchar *uuid;

  gboolean has_au_counters_valid;
  gboolean au_counters_valid;

  gboolean has_error;
  gchar *error;

  guint n_auth_sig_packets;     /* AUTH_SIG packets seen in the stream */
  guint n_timestamp_packets;    /* TIMESTAMP packets seen in the stream */
} Result;

static void
result_clear (Result * res)
{
  g_free (res->cert_status);
  g_free (res->uuid);
  g_free (res->error);
  memset (res, 0, sizeof (*res));
}

static GstBusSyncReply
on_bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  Result *res = user_data;
  const GstStructure *s;
  const gchar *sval;

  if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  s = gst_message_get_structure (msg);
  if (!gst_structure_has_name (s, "mpegh3da-verification-result"))
    return GST_BUS_PASS;

  res->n_results++;
  gst_structure_get_boolean (s, "verified", &res->verified);
  gst_structure_get_uint (s, "signature-size", &res->signature_size);

  res->has_cert_trusted = gst_structure_get_boolean (s, "certificate-trusted",
      &res->cert_trusted);

  sval = gst_structure_get_string (s, "certificate-status");
  if (sval != NULL) {
    res->has_cert_status = TRUE;
    res->cert_status = g_strdup (sval);
  }

  sval = gst_structure_get_string (s, "content-uuid");
  if (sval != NULL) {
    res->has_uuid = TRUE;
    res->uuid = g_strdup (sval);
  }

  res->has_au_counters_valid = gst_structure_get_boolean (s,
      "auth-sequence-au-counters-valid", &res->au_counters_valid);

  sval = gst_structure_get_string (s, "error");
  if (sval != NULL) {
    res->has_error = TRUE;
    res->error = g_strdup (sval);
  }

  return GST_BUS_PASS;
}

/* Minimal MHAS packet builder. All packets used here fit the base widths of
 * the header fields (type <= 6, label <= 2, length <= 0x7ff), so the header
 * is a plain 16-bit big-endian value: type(3) label(2) length(11). */
static GstBuffer *
make_packet (guint64 type, guint64 label, const guint8 * payload, gsize len)
{
  GstBuffer *buf;
  GstMapInfo map;
  guint16 hdr;

  fail_unless (type <= 6 && label <= 2 && len <= 0x7ff);
  hdr = (guint16) ((type << 13) | (label << 11) | (len & 0x7ff));

  buf = gst_buffer_new_allocate (NULL, 2 + len, NULL);
  fail_unless (gst_buffer_map (buf, &map, GST_MAP_WRITE));
  map.data[0] = (guint8) (hdr >> 8);
  map.data[1] = (guint8) hdr;
  if (len)
    memcpy (map.data + 2, payload, len);
  gst_buffer_unmap (buf, &map);

  return buf;
}

/* PACTYP_MPEGH3DAFRAME (2), label 1: one access unit per buffer. */
static GstBuffer *
make_frame (guint8 seed, gsize len)
{
  guint8 *payload = g_malloc (len);
  GstBuffer *buf;

  memset (payload, seed, len);
  buf = make_packet (2, 1, payload, len);
  g_free (payload);
  return buf;
}

/* PACTYP_SYNC (6), label 0, payload 0xA5: the canonical 0xC001A5 sync word. */
static GstBuffer *
make_sync (void)
{
  static const guint8 payload = 0xA5;

  return make_packet (6, 0, &payload, 1);
}

/* PACTYP_MPEGH3DACFG (1), label 0. */
static GstBuffer *
make_config (void)
{
  static const guint8 payload[4] = { 0x00, 0x01, 0x02, 0x03 };

  return make_packet (1, 0, payload, sizeof (payload));
}

/* ---- MHAS packet parsing, mirroring gstmpegh3daparse.c verbatim ---- */

/* escapedValue() reader, identical to gst_mpegh3da_read_escaped_value(). */
static gboolean
read_escaped_value (GstBitReader * br, guint nbits1, guint nbits2,
    guint nbits3, guint64 * value)
{
  guint64 v, add;
  guint64 max1 = (G_GUINT64_CONSTANT (1) << nbits1) - 1;
  guint64 max2 = (G_GUINT64_CONSTANT (1) << nbits2) - 1;

  if (!gst_bit_reader_get_bits_uint64 (br, &v, nbits1))
    return FALSE;

  if (v == max1) {
    if (!gst_bit_reader_get_bits_uint64 (br, &add, nbits2))
      return FALSE;

    v += add;

    if (add == max2) {
      if (!gst_bit_reader_get_bits_uint64 (br, &add, nbits3))
        return FALSE;

      v += add;
    }
  }

  *value = v;
  return TRUE;
}

/* Parse one MHAS packet header (type(3,8,8) label(2,8,32) length(11,24,24))
 * with the same checks as gst_mpegh3da_mhas_parse_packet(). */
static gboolean
parse_packet_header (GstBitReader * br, guint64 * type, guint64 * label,
    guint64 * length)
{
  guint start_pos, header_pos;

  start_pos = gst_bit_reader_get_pos (br);
  if (start_pos % 8 != 0)
    return FALSE;

  if (!read_escaped_value (br, 3, 8, 8, type) ||
      !read_escaped_value (br, 2, 8, 32, label) ||
      !read_escaped_value (br, 11, 24, 24, length))
    return FALSE;

  header_pos = gst_bit_reader_get_pos (br);
  if (header_pos % 8 != 0)
    return FALSE;

  if (*length > gst_bit_reader_get_remaining (br) / 8)
    return FALSE;

  return TRUE;
}

/* Byte offset and total byte length of the first MHAS packet of @type. */
static gboolean
find_mhas_packet (const guint8 * data, gsize size, guint64 type,
    gsize * off, gsize * plen)
{
  GstBitReader br;
  gst_bit_reader_init (&br, data, (guint) size);

  while (gst_bit_reader_get_remaining (&br) != 0) {
    guint start = gst_bit_reader_get_pos (&br);
    guint64 t, label, length;

    if (!parse_packet_header (&br, &t, &label, &length))
      return FALSE;
    if (t == type) {
      *off = start / 8;
      *plen = (gst_bit_reader_get_pos (&br) - start) / 8 + (gsize) length;
      return TRUE;
    }
    if (!gst_bit_reader_skip (&br, (guint) length * 8))
      return FALSE;
  }
  return FALSE;
}

/* Number of MHAS packets of @type in @data. */
static guint
count_mhas_packets (const guint8 * data, gsize size, guint64 type)
{
  GstBitReader br;
  guint count = 0;
  gst_bit_reader_init (&br, data, (guint) size);

  while (gst_bit_reader_get_remaining (&br) != 0) {
    guint64 t, label, length;

    if (!parse_packet_header (&br, &t, &label, &length))
      break;
    if (t == type)
      count++;
    if (!gst_bit_reader_skip (&br, (guint) length * 8))
      break;
  }
  return count;
}

/* Signer/verifier configuration for one roundtrip. */
typedef struct
{
  gint hash_method;
  guint sequence_length;
  guint auth_id;
  gboolean au_counter;
  gboolean timestamp;
  const gchar *content_uuid;
  const gchar *private_key_path;        /* NULL => digest-only */
  const gchar *public_key_uri;
  gboolean fail_on_error;
  const gchar *trust_store_path;
  TamperKind tamper;
} RtConfig;

static RtConfig
base_config (void)
{
  RtConfig c = { 0, };

  c.hash_method = 2;            /* SHA-256 */
  c.sequence_length = 1;
  c.au_counter = TRUE;
  return c;
}

/* Push sequence_length + 1 frames so the first sequence completes (its
 * AUTH_SIG is deferred to the start of the last buffer), then feed the signed
 * buffers to the verifier and capture the result message. */
static void
run_roundtrip (const RtConfig * cfg, Result * res)
{
  GstElement *e;
  GstHarness *sh, *vh;
  GstBus *bus;
  guint i, n_frames = cfg->sequence_length + 1;
  guint n_total = n_frames + 2; /* SYNC + CFG + frames */
  GstBuffer *out[6] = { NULL, };

  fail_unless (n_total <= G_N_ELEMENTS (out));

  e = gst_element_factory_make ("mpegh3dasigner", NULL);
  fail_unless (e != NULL);
  g_object_set (e,
      "hash-method", cfg->hash_method,
      "auth-sequence-length", cfg->sequence_length,
      "auth-id", cfg->auth_id,
      "au-counter", cfg->au_counter, "timestamp", cfg->timestamp, NULL);
  if (cfg->content_uuid != NULL)
    g_object_set (e, "content-uuid", cfg->content_uuid, NULL);
  if (cfg->private_key_path != NULL)
    g_object_set (e, "private-key-path", cfg->private_key_path,
        "public-key-uri", cfg->public_key_uri, NULL);
  sh = gst_harness_new_with_element (e, "sink", "src");
  gst_harness_set_src_caps_str (sh, MHAS_CAPS);
  gst_harness_set_sink_caps_str (sh, MHAS_CAPS);

  e = gst_element_factory_make ("mpegh3daverifier", NULL);
  fail_unless (e != NULL);
  g_object_set (e, "fail-on-verification-error", cfg->fail_on_error, NULL);
  if (cfg->trust_store_path != NULL)
    g_object_set (e, "trust-store-path", cfg->trust_store_path, NULL);
  vh = gst_harness_new_with_element (e, "sink", "src");
  gst_harness_set_src_caps_str (vh, MHAS_CAPS);
  gst_harness_set_sink_caps_str (vh, MHAS_CAPS);

  bus = gst_bus_new ();
  fail_unless (bus != NULL);
  gst_element_set_bus (vh->element, bus);
  gst_bus_set_sync_handler (bus, on_bus_message, res, NULL);

  gst_harness_play (sh);
  gst_harness_play (vh);

  /* Realistic stream: SYNC, MPEGH3DACFG, then one frame per buffer. */
  gst_harness_push (sh, make_sync ());
  out[0] = gst_harness_pull (sh);
  gst_harness_push (sh, make_config ());
  out[1] = gst_harness_pull (sh);
  for (i = 0; i < n_frames; i++) {
    gst_harness_push (sh, make_frame ((guint8) (0xAA + i), 16));
    out[2 + i] = gst_harness_pull (sh);
  }

  if (cfg->tamper == TAMPER_FRAME) {
    GstMapInfo map;

    fail_unless (gst_buffer_map (out[2], &map, GST_MAP_READWRITE));
    map.data[map.size - 1] ^= 0xff;     /* last byte of the first frame */
    gst_buffer_unmap (out[2], &map);
  } else if (cfg->tamper == TAMPER_COUNTER) {
    GstMapInfo map;
    gsize off = 0, len = 0;

    fail_unless (gst_buffer_map (out[3], &map, GST_MAP_READWRITE));
    fail_unless (find_mhas_packet (map.data, map.size, 37, &off, &len));
    map.data[off + len - 1] = 0x00;     /* reset the counter value */
    gst_buffer_unmap (out[3], &map);
  }

  /* Count AUTH_SIG and AUTH_TIMESTAMP packets: digest-only emits 1 complete packet, RSA emits
   * 16 partial segments (2048-bit signature / 16 bytes per segment). */
  for (i = 0; i < n_total; i++) {
    GstMapInfo map;

    if (gst_buffer_map (out[i], &map, GST_MAP_READ)) {
      res->n_auth_sig_packets += count_mhas_packets (map.data, map.size, 33);
      res->n_timestamp_packets += count_mhas_packets (map.data, map.size, 35);
      gst_buffer_unmap (out[i], &map);
    }
  }

  for (i = 0; i < n_total; i++) {
    if (gst_harness_push (vh, out[i]) == GST_FLOW_ERROR)
      res->flow_error = TRUE;
  }

  gst_bus_set_sync_handler (bus, NULL, NULL, NULL);
  gst_element_set_bus (vh->element, NULL);
  gst_object_unref (bus);
  gst_harness_teardown (sh);
  gst_harness_teardown (vh);
}

/* ---- RSA fixture  (real key + self-signed certificate) ---- */

static gboolean
write_rsa_key_and_cert (const gchar * key_path, const gchar * cert_path)
{
  EVP_PKEY_CTX *kctx = NULL;
  EVP_PKEY *pkey = NULL;
  X509 *cert = NULL;
  X509_NAME *name;
  FILE *f;
  gboolean ok = FALSE;

  kctx = EVP_PKEY_CTX_new_id (EVP_PKEY_RSA, NULL);
  if (kctx == NULL)
    return FALSE;
  if (EVP_PKEY_keygen_init (kctx) <= 0)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_keygen_bits (kctx, 2048) <= 0)
    goto out;
  if (EVP_PKEY_keygen (kctx, &pkey) <= 0)
    goto out;

  f = fopen (key_path, "wb");
  if (f == NULL)
    goto out;
  ok = PEM_write_PrivateKey (f, pkey, NULL, NULL, 0, NULL, NULL) == 1;
  fclose (f);
  if (!ok)
    goto out;

  cert = X509_new ();
  if (cert == NULL)
    goto out;
  ASN1_INTEGER_set (X509_get_serialNumber (cert), 1);
  X509_gmtime_adj (X509_getm_notBefore (cert), 0);
  X509_gmtime_adj (X509_getm_notAfter (cert), 3600L * 24 * 365);
  X509_set_pubkey (cert, pkey);
  name = X509_get_subject_name (cert);
  X509_NAME_add_entry_by_txt (name, "CN", MBSTRING_ASC,
      (const unsigned char *) "mpegh3da test", -1, -1, 0);
  X509_set_issuer_name (cert, name);
  if (X509_sign (cert, pkey, EVP_sha256 ()) <= 0)
    goto out;

  f = fopen (cert_path, "wb");
  if (f == NULL)
    goto out;
  ok = PEM_write_X509 (f, cert) == 1;
  fclose (f);

out:
  if (cert != NULL)
    X509_free (cert);
  if (pkey != NULL)
    EVP_PKEY_free (pkey);
  if (kctx != NULL)
    EVP_PKEY_CTX_free (kctx);
  return ok;
}

typedef struct
{
  gchar *dir;
  gchar *key_path;
  gchar *cert_path;
  gchar *trust_dir;
  gchar *empty_dir;
} RsaFixture;

static gboolean
rsa_fixture_setup (RsaFixture * fx)
{
  GError *error = NULL;
  gchar *ca_path;
  gchar *pem;
  gsize len;

  memset (fx, 0, sizeof (*fx));
  fx->dir = g_dir_make_tmp ("mpegh3daauth-XXXXXX", &error);
  if (fx->dir == NULL) {
    g_clear_error (&error);
    return FALSE;
  }
  fx->key_path = g_build_filename (fx->dir, "key.pem", NULL);
  fx->cert_path = g_build_filename (fx->dir, "cert.pem", NULL);
  fx->trust_dir = g_build_filename (fx->dir, "trust", NULL);
  fx->empty_dir = g_build_filename (fx->dir, "empty", NULL);
  g_mkdir_with_parents (fx->trust_dir, 0700);
  g_mkdir_with_parents (fx->empty_dir, 0700);

  if (!write_rsa_key_and_cert (fx->key_path, fx->cert_path))
    return FALSE;

  /* the self-signed certificate acts as its own CA in the trust store */
  if (!g_file_get_contents (fx->cert_path, &pem, &len, &error)) {
    g_clear_error (&error);
    return FALSE;
  }
  ca_path = g_build_filename (fx->trust_dir, "ca.crt", NULL);
  g_file_set_contents (ca_path, pem, len, &error);
  g_clear_error (&error);
  g_free (pem);
  g_free (ca_path);

  return TRUE;
}

static void
rsa_fixture_teardown (RsaFixture * fx)
{
  gchar *ca_path;

  if (fx->dir == NULL)
    return;
  ca_path = g_build_filename (fx->trust_dir, "ca.crt", NULL);
  g_remove (ca_path);
  g_free (ca_path);
  g_remove (fx->cert_path);
  g_remove (fx->key_path);
  g_rmdir (fx->trust_dir);
  g_rmdir (fx->empty_dir);
  g_rmdir (fx->dir);
  g_free (fx->trust_dir);
  g_free (fx->empty_dir);
  g_free (fx->cert_path);
  g_free (fx->key_path);
  g_free (fx->dir);
}

/* ---- tests ---- */

static const gsize digest_sizes[5] = { 20, 28, 32, 48, 64 };

GST_START_TEST (test_roundtrip_all_hash_methods)
{
  gint m;

  for (m = 0; m <= 4; m++) {
    RtConfig cfg = base_config ();
    Result res = { 0, };

    cfg.hash_method = m;
    run_roundtrip (&cfg, &res);

    fail_unless (!res.flow_error, "hash-method %d: unexpected flow error", m);
    fail_unless (res.n_results == 1, "hash-method %d: got %u results",
        m, res.n_results);
    fail_unless (res.verified, "hash-method %d: verification failed", m);
    fail_unless (res.signature_size == digest_sizes[m],
        "hash-method %d: signature size %u, expected %u", m, res.signature_size,
        (guint) digest_sizes[m]);
    /* digest-only message shape: no cert fields, no uuid, valid counters */
    fail_unless (!res.has_cert_trusted && !res.has_cert_status,
        "hash-method %d: digest-only carried certificate fields", m);
    fail_unless (!res.has_uuid, "hash-method %d: unexpected uuid", m);
    fail_unless (res.has_au_counters_valid && res.au_counters_valid,
        "hash-method %d: AU counters missing or invalid", m);
    fail_unless (!res.has_error, "hash-method %d: unexpected error field", m);
    fail_unless (res.n_auth_sig_packets == 1,
        "hash-method %d: got %u AUTH_SIG packets, expected 1", m,
        res.n_auth_sig_packets);

    result_clear (&res);
  }
}

GST_END_TEST;

GST_START_TEST (test_digest_only_with_uuid)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.content_uuid = DEFAULT_UUID;
  run_roundtrip (&cfg, &res);

  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (res.verified, "verification failed");
  fail_unless (res.has_uuid, "content-uuid missing from the message");
  fail_unless (strcmp (res.uuid, DEFAULT_UUID) == 0,
      "content-uuid mismatch: got %s", res.uuid);
  fail_unless (!res.has_cert_trusted && !res.has_cert_status,
      "digest-only carried certificate fields");
  fail_unless (res.has_au_counters_valid && res.au_counters_valid,
      "AU counters missing or invalid");

  result_clear (&res);
}

GST_END_TEST;

GST_START_TEST (test_au_counter_disabled)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.au_counter = FALSE;
  run_roundtrip (&cfg, &res);

  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (res.verified, "verification failed");
  fail_unless (!res.has_au_counters_valid,
      "unexpected AU-counter validity field with au-counter off");

  result_clear (&res);
}

GST_END_TEST;

GST_START_TEST (test_au_counter_tampered)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.sequence_length = 2;
  cfg.tamper = TAMPER_COUNTER;
  run_roundtrip (&cfg, &res);

  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (!res.verified, "verification unexpectedly succeeded");
  fail_unless (res.has_au_counters_valid && !res.au_counters_valid,
      "AU counters not reported invalid");
  fail_unless (res.has_error, "missing error field");

  result_clear (&res);
}

GST_END_TEST;

GST_START_TEST (test_frame_tampered)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.tamper = TAMPER_FRAME;
  run_roundtrip (&cfg, &res);

  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (!res.verified, "verification unexpectedly succeeded");
  fail_unless (res.has_au_counters_valid && res.au_counters_valid,
      "AU counters unexpectedly invalid after frame tamper");
  fail_unless (res.has_error, "missing error field");

  result_clear (&res);
}

GST_END_TEST;

GST_START_TEST (test_fail_on_verification_error)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.tamper = TAMPER_FRAME;
  cfg.fail_on_error = TRUE;
  run_roundtrip (&cfg, &res);

  fail_unless (res.flow_error,
      "no flow error despite fail-on-verification-error");
  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (!res.verified, "verification unexpectedly succeeded");

  result_clear (&res);
}

GST_END_TEST;

GST_START_TEST (test_rsa_roundtrip_unverified_cert)
{
  RsaFixture fx;
  RtConfig cfg = base_config ();
  Result res = { 0, };

  fail_unless (rsa_fixture_setup (&fx));
  cfg.private_key_path = fx.key_path;
  cfg.public_key_uri = fx.cert_path;
  run_roundtrip (&cfg, &res);

  fail_unless (!res.flow_error, "unexpected flow error");
  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (res.verified, "RSA verification failed");
  /* 2048-bit RSA -> PKCS#1 v1.5 signature = 256 bytes (the modulus size),
   * carried as 16 partial AUTH_SIG packets of at most 16 bytes each. */
  fail_unless (res.signature_size == 256,
      "RSA signature size %u, expected 256", res.signature_size);
  fail_unless (res.n_auth_sig_packets == 16,
      "got %u partial AUTH_SIG packets, expected 16", res.n_auth_sig_packets);
  fail_unless (res.has_cert_trusted && !res.cert_trusted,
      "certificate-trusted missing or true");
  fail_unless (res.has_cert_status
      && strcmp (res.cert_status, "Unverified") == 0,
      "cert status %s, expected Unverified", res.cert_status);

  result_clear (&res);
  rsa_fixture_teardown (&fx);
}

GST_END_TEST;

GST_START_TEST (test_rsa_roundtrip_trusted_cert)
{
  RsaFixture fx;
  RtConfig cfg = base_config ();
  Result res = { 0, };

  fail_unless (rsa_fixture_setup (&fx));
  cfg.private_key_path = fx.key_path;
  cfg.public_key_uri = fx.cert_path;
  cfg.trust_store_path = fx.trust_dir;
  run_roundtrip (&cfg, &res);

  fail_unless (!res.flow_error, "unexpected flow error");
  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (res.verified, "RSA verification failed");
  fail_unless (res.has_cert_trusted && res.cert_trusted,
      "certificate not trusted");
  fail_unless (res.has_cert_status
      && strcmp (res.cert_status, "Verified") == 0,
      "cert status %s, expected Verified", res.cert_status);

  result_clear (&res);
  rsa_fixture_teardown (&fx);
}

GST_END_TEST;

GST_START_TEST (test_rsa_roundtrip_untrusted_cert)
{
  RsaFixture fx;
  RtConfig cfg = base_config ();
  Result res = { 0, };

  fail_unless (rsa_fixture_setup (&fx));
  cfg.private_key_path = fx.key_path;
  cfg.public_key_uri = fx.cert_path;
  cfg.trust_store_path = fx.empty_dir;  /* no matching CA */
  run_roundtrip (&cfg, &res);

  fail_unless (!res.flow_error, "unexpected flow error");
  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  /* The RSA signature itself is valid, but the certificate is not issued by
   * any CA in the trust store. */
  fail_unless (res.verified, "signature not cryptographically valid");
  fail_unless (res.has_cert_trusted && !res.cert_trusted,
      "certificate-trusted missing or true");
  fail_unless (res.has_cert_status
      && strcmp (res.cert_status, "Untrusted") == 0,
      "cert status %s, expected Untrusted", res.cert_status);

  result_clear (&res);
  rsa_fixture_teardown (&fx);
}

GST_END_TEST;

GST_START_TEST (test_roundtrip_with_timestamp)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.timestamp = TRUE;
  run_roundtrip (&cfg, &res);

  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (res.verified, "verification with timestamp failed");
  /* one authTimeLong TIMESTAMP before each frame (2 frames here) */
  fail_unless (res.n_timestamp_packets == 2,
      "got %u TIMESTAMP packets, expected 2", res.n_timestamp_packets);

  result_clear (&res);
}

GST_END_TEST;

GST_START_TEST (test_roundtrip_with_auth_id)
{
  RtConfig cfg = base_config ();
  Result res = { 0, };

  cfg.auth_id = 7;
  run_roundtrip (&cfg, &res);

  fail_unless (res.n_results == 1, "got %u results", res.n_results);
  fail_unless (res.verified, "verification with non-default auth-id failed");

  result_clear (&res);
}

GST_END_TEST;

static Suite *
mpegh3daauth_suite (void)
{
  Suite *s = suite_create ("mpegh3daauth");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_roundtrip_all_hash_methods);
  tcase_add_test (tc, test_digest_only_with_uuid);
  tcase_add_test (tc, test_au_counter_disabled);
  tcase_add_test (tc, test_au_counter_tampered);
  tcase_add_test (tc, test_frame_tampered);
  tcase_add_test (tc, test_fail_on_verification_error);
  tcase_add_test (tc, test_rsa_roundtrip_unverified_cert);
  tcase_add_test (tc, test_rsa_roundtrip_trusted_cert);
  tcase_add_test (tc, test_rsa_roundtrip_untrusted_cert);
  tcase_add_test (tc, test_roundtrip_with_timestamp);
  tcase_add_test (tc, test_roundtrip_with_auth_id);

  return s;
}

GST_CHECK_MAIN (mpegh3daauth);
