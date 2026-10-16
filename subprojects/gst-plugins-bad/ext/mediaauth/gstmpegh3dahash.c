/* TODO: add copyright and license header */

#include "gstmpegh3dahash.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

struct _GstMpegh3daHash
{
  GChecksum *checksum;
  gsize digest_len;
};

static gboolean
hash_method_to_gchecksum (GstMpegh3daHashMethod method, GChecksumType * ctype,
    gsize * digest_len)
{
  switch (method) {
    case GST_MPEGH3DA_HASH_SHA1:
      *ctype = G_CHECKSUM_SHA1;
      *digest_len = 20;
      return TRUE;
    case GST_MPEGH3DA_HASH_SHA256:
      *ctype = G_CHECKSUM_SHA256;
      *digest_len = 32;
      return TRUE;
    case GST_MPEGH3DA_HASH_SHA384:
      *ctype = G_CHECKSUM_SHA384;
      *digest_len = 48;
      return TRUE;
    case GST_MPEGH3DA_HASH_SHA512:
      *ctype = G_CHECKSUM_SHA512;
      *digest_len = 64;
      return TRUE;
    case GST_MPEGH3DA_HASH_SHA224:
      GST_ERROR ("SHA-224 hash method is not implemented");
      return FALSE;
    default:
      return FALSE;
  }
}

GstMpegh3daHash *
gst_mpegh3da_hash_new (GstMpegh3daHashMethod method)
{
  GstMpegh3daHash *hash;
  GChecksumType ctype;
  gsize digest_len;

  if (!hash_method_to_gchecksum (method, &ctype, &digest_len))
    return NULL;

  hash = g_new0 (GstMpegh3daHash, 1);
  hash->checksum = g_checksum_new (ctype);
  if (!hash->checksum) {
    g_free (hash);
    return NULL;
  }
  hash->digest_len = digest_len;

  return hash;
}

void
gst_mpegh3da_hash_free (GstMpegh3daHash * hash)
{
  if (!hash)
    return;

  if (hash->checksum)
    g_checksum_free (hash->checksum);
  g_free (hash);
}

gboolean
gst_mpegh3da_hash_update (GstMpegh3daHash * hash, const guint8 * data,
    gsize size)
{
  g_return_val_if_fail (hash != NULL, FALSE);
  g_return_val_if_fail (hash->checksum != NULL, FALSE);
  g_return_val_if_fail (data != NULL || size == 0, FALSE);

  if (size == 0)
    return TRUE;

  g_checksum_update (hash->checksum, data, size);
  return TRUE;
}

gboolean
gst_mpegh3da_hash_finish (GstMpegh3daHash * hash, guint8 * digest,
    gsize * digest_size)
{
  gsize len;

  g_return_val_if_fail (hash != NULL, FALSE);
  g_return_val_if_fail (hash->checksum != NULL, FALSE);
  g_return_val_if_fail (digest != NULL, FALSE);
  g_return_val_if_fail (digest_size != NULL, FALSE);

  len = hash->digest_len;
  g_checksum_get_digest (hash->checksum, digest, &len);
  *digest_size = len;

  return TRUE;
}

gsize
gst_mpegh3da_hash_get_digest_size (GstMpegh3daHash * hash)
{
  g_return_val_if_fail (hash != NULL, 0);

  return hash->digest_len;
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
