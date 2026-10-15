/* TODO: add copyright and license header */

#include <errno.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "gstmpegh3dacrypto.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

/* Log the last entry of the OpenSSL error queue. */
static void
log_openssl_error (GstObject * object, const gchar * what)
{
  unsigned long err = ERR_get_error ();
  gchar buf[256];

  if (err != 0) {
    ERR_error_string_n (err, buf, sizeof (buf));
    GST_ERROR_OBJECT (object, "%s: %s", what, buf);
  } else {
    GST_ERROR_OBJECT (object, "%s", what);
  }
}

const EVP_MD *
gst_mpegh3da_crypto_evp_md (GstMpegh3daHashMethod method)
{
  switch (method) {
    case GST_MPEGH3DA_HASH_SHA1:
      return EVP_sha1 ();
    case GST_MPEGH3DA_HASH_SHA224:
      return EVP_sha224 ();
    case GST_MPEGH3DA_HASH_SHA256:
      return EVP_sha256 ();
    case GST_MPEGH3DA_HASH_SHA384:
      return EVP_sha384 ();
    case GST_MPEGH3DA_HASH_SHA512:
      return EVP_sha512 ();
    default:
      return NULL;
  }
}

gsize
gst_mpegh3da_crypto_signature_size (EVP_PKEY * pkey)
{
  g_return_val_if_fail (pkey != NULL, 0);

  return (gsize) EVP_PKEY_size (pkey);
}

EVP_PKEY *
gst_mpegh3da_crypto_load_private_key (GstObject * object, const gchar * path)
{
  BIO *bio;
  EVP_PKEY *pkey;

  g_return_val_if_fail (path != NULL, NULL);

  bio = BIO_new_file (path, "r");
  if (bio == NULL) {
    GST_ERROR_OBJECT (object, "failed to open private key file \"%s\": %s",
        path, g_strerror (errno));
    return NULL;
  }

  pkey = PEM_read_bio_PrivateKey (bio, NULL, NULL, NULL);
  BIO_free (bio);

  if (pkey == NULL) {
    GST_ERROR_OBJECT (object, "failed to read private key \"%s\" "
        "(expected PKCS#8 or PKCS#1 PEM)", path);
    log_openssl_error (object, "PEM_read_bio_PrivateKey failed");
    return NULL;
  }

  return pkey;
}

const gchar *
gst_mpegh3da_cert_status_name (GstMpegh3daCertStatus status)
{
  switch (status) {
    case GST_MPEGH3DA_CERT_VERIFIED:
      return "Verified";
    case GST_MPEGH3DA_CERT_UNTRUSTED:
      return "Untrusted";
    case GST_MPEGH3DA_CERT_ERROR:
      return "Error";
    case GST_MPEGH3DA_CERT_UNVERIFIED:
    default:
      return "Unverified";
  }
}

/* Read the first X.509 certificate from a PEM file. Failures are logged at
 * DEBUG level only: the trust-store scan must not spam errors for unrelated
 * files, and the callers decide whether the failure is fatal. */
static X509 *
read_x509_from_file (GstObject * object, const gchar * path)
{
  BIO *bio;
  X509 *cert;

  bio = BIO_new_file (path, "r");
  if (bio == NULL) {
    GST_DEBUG_OBJECT (object, "failed to open certificate file \"%s\": %s",
        path, g_strerror (errno));
    return NULL;
  }

  cert = PEM_read_bio_X509 (bio, NULL, NULL, NULL);
  BIO_free (bio);

  if (cert == NULL)
    GST_DEBUG_OBJECT (object, "failed to parse X.509 certificate \"%s\"", path);

  return cert;
}

/* Verify @leaf against the CA certificates in @trust_store_path: a CA whose
 * subject matches the certificate's issuer and whose key verifies the
 * certificate signature marks it trusted. No chain building, validity or
 * basic-constraints checks are performed. */
static GstMpegh3daCertStatus
verify_certificate_against_trust_store (GstObject * object, X509 * leaf,
    const gchar * trust_store_path)
{
  GDir *dir;
  const gchar *name;
  GError *error = NULL;
  X509_NAME *issuer;
  GstMpegh3daCertStatus status = GST_MPEGH3DA_CERT_UNTRUSTED;

  if (trust_store_path == NULL || *trust_store_path == '\0') {
    GST_WARNING_OBJECT (object, "no trust-store-path configured, skipping "
        "certificate validation");
    return GST_MPEGH3DA_CERT_UNVERIFIED;
  }

  if (!g_file_test (trust_store_path, G_FILE_TEST_IS_DIR)) {
    GST_ERROR_OBJECT (object, "trust-store-path \"%s\" is not a directory",
        trust_store_path);
    return GST_MPEGH3DA_CERT_ERROR;
  }

  dir = g_dir_open (trust_store_path, 0, &error);
  if (dir == NULL) {
    GST_ERROR_OBJECT (object, "failed to read trust-store-path \"%s\": %s",
        trust_store_path, error->message);
    g_clear_error (&error);
    return GST_MPEGH3DA_CERT_ERROR;
  }

  issuer = X509_get_issuer_name (leaf);

  while ((name = g_dir_read_name (dir)) != NULL) {
    X509 *ca;
    EVP_PKEY *ca_key;
    gchar *ca_path;

    if (!g_str_has_suffix (name, ".crt") && !g_str_has_suffix (name, ".pem"))
      continue;

    ca_path = g_build_filename (trust_store_path, name, NULL);
    ca = read_x509_from_file (object, ca_path);
    if (ca == NULL) {
      /* Drop the OpenSSL error queue so it does not leak into the next CA. */
      ERR_clear_error ();
      g_free (ca_path);
      continue;
    }

    if (X509_NAME_cmp (X509_get_subject_name (ca), issuer) == 0) {
      ca_key = X509_get_pubkey (ca);
      if (ca_key != NULL) {
        if (X509_verify (leaf, ca_key) == 1) {
          GST_INFO_OBJECT (object, "certificate verified against CA \"%s\"",
              ca_path);
          status = GST_MPEGH3DA_CERT_VERIFIED;
        } else {
          ERR_clear_error ();
        }
        EVP_PKEY_free (ca_key);
      }
    }

    X509_free (ca);
    g_free (ca_path);

    if (status == GST_MPEGH3DA_CERT_VERIFIED)
      break;
  }

  g_dir_close (dir);

  if (status == GST_MPEGH3DA_CERT_UNTRUSTED)
    GST_WARNING_OBJECT (object, "no trusted CA found for certificate issuer "
        "in \"%s\"", trust_store_path);

  return status;
}

EVP_PKEY *
gst_mpegh3da_crypto_load_public_key_from_cert_checked (GstObject * object,
    const gchar * path, const gchar * trust_store_path,
    GstMpegh3daCertStatus * cert_status)
{
  X509 *cert;
  EVP_PKEY *pkey;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (cert_status != NULL, NULL);

  *cert_status = GST_MPEGH3DA_CERT_ERROR;

  cert = read_x509_from_file (object, path);
  if (cert != NULL) {
    *cert_status = verify_certificate_against_trust_store (object, cert,
        trust_store_path);

    pkey = X509_get_pubkey (cert);
    X509_free (cert);

    if (pkey == NULL) {
      GST_ERROR_OBJECT (object, "failed to extract public key from \"%s\"",
          path);
      log_openssl_error (object, "X509_get_pubkey failed");
      *cert_status = GST_MPEGH3DA_CERT_ERROR;
    }

    return pkey;
  }

  /* Not an X.509 certificate: accept a raw PEM public key. There is no
   * certificate to validate, so the trust status stays unverified. */
  ERR_clear_error ();
  pkey = gst_mpegh3da_crypto_load_public_key (object, path);
  if (pkey == NULL) {
    GST_ERROR_OBJECT (object, "failed to load an X.509 certificate or a "
        "public key from \"%s\"", path);
    *cert_status = GST_MPEGH3DA_CERT_ERROR;
    return NULL;
  }

  *cert_status = GST_MPEGH3DA_CERT_UNVERIFIED;
  GST_INFO_OBJECT (object, "loaded raw public key from \"%s\" (no "
      "certificate trust validation)", path);

  return pkey;
}

EVP_PKEY *
gst_mpegh3da_crypto_load_public_key (GstObject * object, const gchar * path)
{
  BIO *bio;
  EVP_PKEY *pkey;

  g_return_val_if_fail (path != NULL, NULL);

  bio = BIO_new_file (path, "r");
  if (bio == NULL) {
    GST_ERROR_OBJECT (object, "failed to open public key file \"%s\": %s",
        path, g_strerror (errno));
    return NULL;
  }

  pkey = PEM_read_bio_PUBKEY (bio, NULL, NULL, NULL);
  BIO_free (bio);

  if (pkey == NULL) {
    GST_ERROR_OBJECT (object, "failed to read public key \"%s\" "
        "(expected \"PUBLIC KEY\" or \"RSA PUBLIC KEY\" PEM)", path);
    log_openssl_error (object, "PEM_read_bio_PUBKEY failed");
    return NULL;
  }

  return pkey;
}

struct _GstMpegh3daCryptoCtx
{
  EVP_MD_CTX *ctx;
  GstMpegh3daCryptoMode mode;
};

GstMpegh3daCryptoCtx *
gst_mpegh3da_crypto_new (GstObject * object, GstMpegh3daCryptoMode mode,
    EVP_PKEY * pkey, GstMpegh3daHashMethod method)
{
  GstMpegh3daCryptoCtx *ctx;
  const EVP_MD *md;
  gint ok = 0;

  md = gst_mpegh3da_crypto_evp_md (method);
  if (md == NULL) {
    GST_ERROR_OBJECT (object, "unsupported hash method %d", method);
    return NULL;
  }

  ctx = g_new0 (GstMpegh3daCryptoCtx, 1);
  ctx->mode = mode;
  ctx->ctx = EVP_MD_CTX_new ();
  if (ctx->ctx == NULL) {
    g_free (ctx);
    return NULL;
  }

  switch (mode) {
    case GST_MPEGH3DA_CRYPTO_MODE_SIGN:
      if (pkey == NULL) {
        GST_ERROR_OBJECT (object, "sign mode requires a private key");
        gst_mpegh3da_crypto_ctx_free (ctx);
        return NULL;
      }
      ok = EVP_DigestSignInit (ctx->ctx, NULL, md, NULL, pkey);
      break;
    case GST_MPEGH3DA_CRYPTO_MODE_VERIFY:
      if (pkey == NULL) {
        GST_ERROR_OBJECT (object, "verify mode requires a public key");
        gst_mpegh3da_crypto_ctx_free (ctx);
        return NULL;
      }
      ok = EVP_DigestVerifyInit (ctx->ctx, NULL, md, NULL, pkey);
      break;
    case GST_MPEGH3DA_CRYPTO_MODE_DIGEST:
    default:
      ok = EVP_DigestInit_ex (ctx->ctx, md, NULL);
      break;
  }

  if (ok != 1) {
    GST_ERROR_OBJECT (object, "failed to initialize crypto context "
        "(mode %d)", mode);
    log_openssl_error (object, "crypto context init failed");
    gst_mpegh3da_crypto_ctx_free (ctx);
    return NULL;
  }

  return ctx;
}

void
gst_mpegh3da_crypto_ctx_free (GstMpegh3daCryptoCtx * ctx)
{
  if (ctx == NULL)
    return;

  if (ctx->ctx)
    EVP_MD_CTX_free (ctx->ctx);
  g_free (ctx);
}

gboolean
gst_mpegh3da_crypto_update (GstMpegh3daCryptoCtx * ctx,
    const guint8 * data, gsize len)
{
  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (ctx->ctx != NULL, FALSE);
  g_return_val_if_fail (data != NULL || len == 0, FALSE);

  if (len == 0)
    return TRUE;

  switch (ctx->mode) {
    case GST_MPEGH3DA_CRYPTO_MODE_SIGN:
      return EVP_DigestSignUpdate (ctx->ctx, data, len) == 1;
    case GST_MPEGH3DA_CRYPTO_MODE_VERIFY:
      return EVP_DigestVerifyUpdate (ctx->ctx, data, len) == 1;
    case GST_MPEGH3DA_CRYPTO_MODE_DIGEST:
    default:
      return EVP_DigestUpdate (ctx->ctx, data, len) == 1;
  }
}

gboolean
gst_mpegh3da_crypto_sign_final (GstMpegh3daCryptoCtx * ctx, guint8 * sig,
    gsize * sig_len)
{
  size_t slen;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (ctx->ctx != NULL, FALSE);
  g_return_val_if_fail (sig != NULL, FALSE);
  g_return_val_if_fail (sig_len != NULL, FALSE);

  slen = *sig_len;
  if (EVP_DigestSignFinal (ctx->ctx, sig, &slen) != 1)
    return FALSE;

  *sig_len = slen;
  return TRUE;
}

gboolean
gst_mpegh3da_crypto_verify_final (GstMpegh3daCryptoCtx * ctx,
    const guint8 * sig, gsize sig_len, gboolean * error_out)
{
  int ret;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (ctx->ctx != NULL, FALSE);
  g_return_val_if_fail (sig != NULL, FALSE);

  ret = EVP_DigestVerifyFinal (ctx->ctx, sig, sig_len);

  if (error_out != NULL)
    *error_out = (ret < 0);

  return ret == 1;
}

gboolean
gst_mpegh3da_crypto_digest_final (GstMpegh3daCryptoCtx * ctx,
    guint8 * digest, gsize * digest_size)
{
  unsigned int len = 0;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (ctx->ctx != NULL, FALSE);
  g_return_val_if_fail (digest != NULL, FALSE);
  g_return_val_if_fail (digest_size != NULL, FALSE);

  if (EVP_DigestFinal_ex (ctx->ctx, digest, &len) != 1)
    return FALSE;

  *digest_size = len;
  return TRUE;
}

gsize
gst_mpegh3da_crypto_digest_size (GstMpegh3daCryptoCtx * ctx)
{
  g_return_val_if_fail (ctx != NULL, 0);
  g_return_val_if_fail (ctx->ctx != NULL, 0);

  return (gsize) EVP_MD_CTX_size (ctx->ctx);
}

void
gst_mpegh3da_crypto_log_digest (GstObject * object, const guint8 * digest,
    gsize digest_len)
{
  gchar hex[GST_MPEGH3DA_HASH_MAX_DIGEST_BYTES * 2 + 1];
  gsize i;

  g_return_if_fail (digest != NULL);

  hex[0] = '\0';
  for (i = 0; i < digest_len; i++)
    g_snprintf (hex + 2 * i, 3, "%02x", (guint) digest[i]);

  GST_DEBUG_OBJECT (object, "digest: %s", hex);
}
