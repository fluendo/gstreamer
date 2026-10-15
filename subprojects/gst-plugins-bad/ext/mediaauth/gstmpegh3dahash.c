/* TODO: add copyright and license header */

#include <openssl/evp.h>

#include "gstmpegh3dacrypto.h"
#include "gstmpegh3dahash.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

/* Incremental digest hasher backed by the OpenSSL EVP digest API. It shares
 * the hash-method -> digest mapping with the RSA sign/verify backend in
 * gstmpegh3dacrypto.c (single source of truth, including SHA-224). */
struct _GstMpegh3daHash
{
  EVP_MD_CTX *ctx;
  const EVP_MD *md;
};

GstMpegh3daHash *
gst_mpegh3da_hash_new (GstMpegh3daHashMethod method)
{
  GstMpegh3daHash *hash;
  const EVP_MD *md;

  md = gst_mpegh3da_crypto_evp_md (method);
  if (md == NULL) {
    GST_ERROR ("unsupported hash method %d", method);
    return NULL;
  }

  hash = g_new0 (GstMpegh3daHash, 1);
  hash->md = md;
  hash->ctx = EVP_MD_CTX_new ();
  if (hash->ctx == NULL) {
    g_free (hash);
    return NULL;
  }

  if (EVP_DigestInit_ex (hash->ctx, md, NULL) != 1) {
    EVP_MD_CTX_free (hash->ctx);
    g_free (hash);
    return NULL;
  }

  return hash;
}

void
gst_mpegh3da_hash_free (GstMpegh3daHash * hash)
{
  if (!hash)
    return;

  if (hash->ctx)
    EVP_MD_CTX_free (hash->ctx);
  g_free (hash);
}

gboolean
gst_mpegh3da_hash_update (GstMpegh3daHash * hash, const guint8 * data,
    gsize size)
{
  g_return_val_if_fail (hash != NULL, FALSE);
  g_return_val_if_fail (hash->ctx != NULL, FALSE);
  g_return_val_if_fail (data != NULL || size == 0, FALSE);

  if (size == 0)
    return TRUE;

  return EVP_DigestUpdate (hash->ctx, data, size) == 1;
}

gboolean
gst_mpegh3da_hash_finish (GstMpegh3daHash * hash, guint8 * digest,
    gsize * digest_size)
{
  unsigned int len = 0;

  g_return_val_if_fail (hash != NULL, FALSE);
  g_return_val_if_fail (hash->ctx != NULL, FALSE);
  g_return_val_if_fail (digest != NULL, FALSE);
  g_return_val_if_fail (digest_size != NULL, FALSE);

  if (EVP_DigestFinal_ex (hash->ctx, digest, &len) != 1)
    return FALSE;

  *digest_size = len;
  return TRUE;
}

gsize
gst_mpegh3da_hash_get_digest_size (GstMpegh3daHash * hash)
{
  g_return_val_if_fail (hash != NULL, 0);
  g_return_val_if_fail (hash->md != NULL, 0);

  return (gsize) EVP_MD_size (hash->md);
}

/* Log a digest in hexadecimal using the object's debug category. */
void
gst_mpegh3da_hash_log_digest (GstObject * object, const guint8 * digest,
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
