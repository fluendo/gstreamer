/* TODO: add copyright and license header */

#include <string.h>

#include "gstmpegh3daauthdata.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

void
gst_mpegh3da_auth_data_init (GstMpegh3daAuthData * data)
{
  g_return_if_fail (data != NULL);

  memset (data, 0, sizeof (*data));
}

void
gst_mpegh3da_auth_data_clear (GstMpegh3daAuthData * data)
{
  g_return_if_fail (data != NULL);

  g_clear_pointer (&data->hash, gst_mpegh3da_crypto_ctx_free);
  g_clear_pointer (&data->public_key, EVP_PKEY_free);
  data->rsa_mode = FALSE;
  data->signature_len = 0;
}

gboolean
gst_mpegh3da_auth_data_start_hash (GstMpegh3daAuthData * data,
    GstObject * object)
{
  g_return_val_if_fail (data != NULL, FALSE);

  g_clear_pointer (&data->hash, gst_mpegh3da_crypto_ctx_free);
  data->rsa_mode = FALSE;

  data->hash = gst_mpegh3da_crypto_new (object,
      GST_MPEGH3DA_CRYPTO_MODE_DIGEST, NULL,
      (GstMpegh3daHashMethod) data->auth_start.authHashType);

  return data->hash != NULL;
}

gboolean
gst_mpegh3da_auth_data_start_verify (GstMpegh3daAuthData * data,
    GstObject * object, EVP_PKEY * public_key)
{
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (public_key != NULL, FALSE);

  g_clear_pointer (&data->hash, gst_mpegh3da_crypto_ctx_free);
  g_clear_pointer (&data->public_key, EVP_PKEY_free);

  data->public_key = public_key;
  EVP_PKEY_up_ref (data->public_key);   /* owned reference */
  data->hash = gst_mpegh3da_crypto_new (object,
      GST_MPEGH3DA_CRYPTO_MODE_VERIFY, data->public_key,
      (GstMpegh3daHashMethod) data->auth_start.authHashType);
  if (data->hash == NULL) {
    g_clear_pointer (&data->public_key, EVP_PKEY_free);
    return FALSE;
  }

  data->rsa_mode = TRUE;
  data->signature_len = 0;

  return TRUE;
}

gboolean
gst_mpegh3da_auth_data_hash_update (GstMpegh3daAuthData * data,
    const guint8 * bytes, gsize len)
{
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (data->hash != NULL, FALSE);
  g_return_val_if_fail (bytes != NULL || len == 0, FALSE);

  return gst_mpegh3da_crypto_update (data->hash, bytes, len);
}

gboolean
gst_mpegh3da_auth_data_hash_finish (GstMpegh3daAuthData * data,
    guint8 * digest, gsize * digest_size)
{
  gboolean ok;
  guint uuid_len;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (data->hash != NULL, FALSE);
  g_return_val_if_fail (digest != NULL, FALSE);
  g_return_val_if_fail (digest_size != NULL, FALSE);

  if (data->auth_uuid_present) {
    uuid_len = (guint) data->auth_uuid.uuidSegmentLengthMinus1 + 1;
    if (!gst_mpegh3da_crypto_update (data->hash, data->auth_uuid.uuid,
            uuid_len))
      return FALSE;
  }

  ok = gst_mpegh3da_crypto_digest_final (data->hash, digest, digest_size);
  g_clear_pointer (&data->hash, gst_mpegh3da_crypto_ctx_free);

  return ok;
}

gboolean
gst_mpegh3da_auth_data_append_signature (GstMpegh3daAuthData * data,
    const guint8 * seg, gsize len)
{
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (seg != NULL || len == 0, FALSE);

  if (len > sizeof (data->signature) - data->signature_len)
    return FALSE;

  memcpy (data->signature + data->signature_len, seg, len);
  data->signature_len += len;

  return TRUE;
}

void
gst_mpegh3da_auth_data_validate_au_counter (GstMpegh3daAuthData * data,
    GstObject * object, guint32 counter)
{
  g_return_if_fail (data != NULL);

  /* §17.12.4.1: when hasAuthSequenceAUCounter is 0 the sequence must not
   * carry counter packets; otherwise their values must be strictly
   * increasing. */
  if (!data->auth_start.hasAuthSequenceAUCounter) {
    GST_WARNING_OBJECT (object, "unexpected authSequenceAUCounter packet "
        "(hasAuthSequenceAUCounter is 0)");
    data->auth_sequence_au_counters_valid = FALSE;
    return;
  }

  if (data->au_counter_prev_valid) {
    if (counter <= data->au_counter_prev) {
      GST_WARNING_OBJECT (object, "authSequenceAUCounter %u is not strictly "
          "increasing (previous was %u)", counter, data->au_counter_prev);
      data->auth_sequence_au_counters_valid = FALSE;
    }
  } else {
    /* First counter of the sequence: valid until proven otherwise. */
    data->auth_sequence_au_counters_valid = TRUE;
  }

  data->au_counter_prev = counter;
  data->au_counter_prev_valid = TRUE;
}

gboolean
gst_mpegh3da_auth_data_verify_finish (GstMpegh3daAuthData * data,
    GstObject * object, gboolean * error_out)
{
  gboolean ok;
  guint uuid_len;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (data->hash != NULL, FALSE);
  g_return_val_if_fail (error_out != NULL, FALSE);

  if (data->auth_uuid_present) {
    uuid_len = (guint) data->auth_uuid.uuidSegmentLengthMinus1 + 1;
    if (!gst_mpegh3da_crypto_update (data->hash, data->auth_uuid.uuid,
            uuid_len))
      return FALSE;
  }

  ok = gst_mpegh3da_crypto_verify_final (data->hash, data->signature,
      data->signature_len, error_out);
  g_clear_pointer (&data->hash, gst_mpegh3da_crypto_ctx_free);

  return ok;
}

gboolean
gst_mpegh3da_auth_data_verify_signature (GstObject * object,
    const GstMpegh3daAuthData * data, const guint8 * digest, gsize digest_size)
{
  gsize sig_len;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (digest != NULL || digest_size == 0, FALSE);

  /* Only digest-only signing is supported: authProvID = 1 (Message Digest
   * only) with authKeyID = 0 (no key). */
  if (data->auth_start.authProvID != 1) {
    GST_WARNING_OBJECT (object, "unsupported authProvID %" G_GUINT64_FORMAT
        " (only digest-only, authProvID=1, is supported)",
        data->auth_start.authProvID);
    return FALSE;
  }
  if (data->auth_start.authKeyID != 0) {
    GST_WARNING_OBJECT (object, "unsupported authKeyID %" G_GUINT64_FORMAT
        " (only digest-only, authKeyID=0, is supported)",
        data->auth_start.authKeyID);
    return FALSE;
  }

  /* Only complete, non-ABR signatures are supported. */
  if (data->auth_sig.authPartialSig != 0) {
    GST_WARNING_OBJECT (object, "partial signatures are not supported");
    return FALSE;
  }
  if (data->auth_sig.authABREnable != 0) {
    GST_WARNING_OBJECT (object, "ABR signatures are not supported");
    return FALSE;
  }

  sig_len = (gsize) data->auth_sig.sigLengthMinus1 + 1;

  return (sig_len == digest_size
      && memcmp (digest, data->auth_sig.sigComplete, sig_len) == 0);
}

void
gst_mpegh3da_auth_data_log (GstObject * object,
    const GstMpegh3daAuthData * data)
{
  g_return_if_fail (data != NULL);

  GST_DEBUG_OBJECT (object, "GetAuthData: authID=%u authSequence=%u "
      "authHashType=%" G_GUINT64_FORMAT " authUUIDPresent=%d "
      "authTimestampPresent=%d authSequenceAUCountersValid=%d "
      "authTempCons=%d authStartNew=%d byteAlignment=%u "
      "authResilienceLevelMax=%u",
      data->auth_start.authID, data->auth_start.authSequence,
      data->auth_start.authHashType,
      data->auth_uuid_present, data->auth_timestamp_present,
      data->auth_sequence_au_counters_valid, data->auth_temp_cons,
      data->auth_start_new, data->byte_alignment,
      data->auth_start.authResilienceLevelMax);
}
