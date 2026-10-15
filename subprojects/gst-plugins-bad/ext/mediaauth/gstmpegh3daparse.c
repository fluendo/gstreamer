/* TODO: add copyright and license header */

#include "gstmpegh3daparse.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

gboolean
gst_mpegh3da_read_escaped_value (GstBitReader * br, guint nbits1,
    guint nbits2, guint nbits3, guint64 * value)
{
  guint64 v, add;
  guint64 max1 = (G_GUINT64_CONSTANT (1) << nbits1) - 1;
  guint64 max2 = (G_GUINT64_CONSTANT (1) << nbits2) - 1;

  g_return_val_if_fail (br != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

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

gboolean
gst_mpegh3da_mhas_parse_packet (GstBitReader * br, GstMpegh3daMhasPacket * pkt)
{
  guint start_pos, header_pos;
  guint64 type, label, length;

  g_return_val_if_fail (br != NULL, FALSE);
  g_return_val_if_fail (pkt != NULL, FALSE);

  /* Packets start byte-aligned (Table 222 NOTE). */
  start_pos = gst_bit_reader_get_pos (br);
  if (start_pos % 8 != 0)
    return FALSE;

  if (!gst_mpegh3da_read_escaped_value (br, GST_MPEGH3DA_ESC_VAL_MHAS_TYPE,
          &type) ||
      !gst_mpegh3da_read_escaped_value (br, GST_MPEGH3DA_ESC_VAL_MHAS_LABEL,
          &label) ||
      !gst_mpegh3da_read_escaped_value (br, GST_MPEGH3DA_ESC_VAL_MHAS_LENGTH,
          &length))
    return FALSE;

  /* The header always sums to whole bytes (Table 222 NOTE). */
  header_pos = gst_bit_reader_get_pos (br);
  if (header_pos % 8 != 0)
    return FALSE;

  /* Verify the declared payload length fits in the remaining bytes. */
  if (length > gst_bit_reader_get_remaining (br) / 8)
    return FALSE;

  pkt->type = type;
  pkt->label = label;
  pkt->length = length;
  pkt->header_size = (header_pos - start_pos) / 8;
  pkt->payload = br->data + (header_pos / 8);

  /* Skip the byte-aligned payload. */
  return gst_bit_reader_skip (br, (guint) length * 8);
}

/* Log every MHAS packet (header + payload length) in a byte buffer. */
void
gst_mpegh3da_mhas_log_buffer (GstObject * object, const guint8 * data,
    gsize size, const gchar * title)
{
  GstBitReader br;
  guint count = 0;

  g_return_if_fail (data != NULL || size == 0);
  g_return_if_fail (title != NULL);

  GST_DEBUG_OBJECT (object, "--- %s (%" G_GSIZE_FORMAT " bytes) ---",
      title, size);

  gst_bit_reader_init (&br, data, (guint) size);
  while (gst_bit_reader_get_remaining (&br) != 0) {
    GstMpegh3daMhasPacket pkt;

    if (!gst_mpegh3da_mhas_parse_packet (&br, &pkt)) {
      GST_DEBUG_OBJECT (object, "malformed MHAS packet at byte %u",
          gst_bit_reader_get_pos (&br) / 8);
      break;
    }
    gst_mpegh3da_mhas_log_packet (object, &pkt);
    count++;
  }

  GST_DEBUG_OBJECT (object, "--- %s: %u packet%s ---", title, count,
      count == 1 ? "" : "s");
}

static const gchar *
hash_method_name (guint64 method)
{
  switch (method) {
    case GST_MPEGH3DA_HASH_SHA1:
      return "SHA-1";
    case GST_MPEGH3DA_HASH_SHA224:
      return "SHA-224";
    case GST_MPEGH3DA_HASH_SHA256:
      return "SHA-256";
    case GST_MPEGH3DA_HASH_SHA384:
      return "SHA-384";
    case GST_MPEGH3DA_HASH_SHA512:
      return "SHA-512";
    default:
      return "unknown";
  }
}

static void
log_bytes_hex (GstObject * object, const gchar * label, const guint8 * data,
    gsize len)
{
  gchar hex[GST_MPEGH3DA_HASH_MAX_DIGEST_BYTES * 2 + 1];
  gsize i;

  if (len == 0 || len > GST_MPEGH3DA_HASH_MAX_DIGEST_BYTES) {
    GST_DEBUG_OBJECT (object, "  %s: (empty)", label);
    return;
  }

  for (i = 0; i < len; i++)
    g_snprintf (hex + 2 * i, 3, "%02x", (guint) data[i]);
  hex[len * 2] = '\0';

  GST_DEBUG_OBJECT (object, "  %s: %s", label, hex);
}

gboolean
gst_mpegh3da_parse_AuthStart (GstObject * object, const guint8 * data,
    gsize size, GstMpegh3daAuthStart * cfg)
{
  GstBitReader br;
  guint i;

  g_return_val_if_fail (cfg != NULL, FALSE);

  gst_bit_reader_init (&br, data, (guint) size);

  if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->authID, 8) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authSequence, 1) ||
      !gst_mpegh3da_read_escaped_value (&br, GST_MPEGH3DA_ESC_VAL_AUTH_HASH,
          &cfg->authHashType) ||
      !gst_mpegh3da_read_escaped_value (&br, GST_MPEGH3DA_ESC_VAL_AUTH_KEYID,
          &cfg->authKeyID) ||
      !gst_mpegh3da_read_escaped_value (&br, GST_MPEGH3DA_ESC_VAL_AUTH_PROVID,
          &cfg->authProvID))
    return FALSE;

  if (cfg->authProvID == 0) {
    if (!gst_mpegh3da_read_escaped_value (&br,
            GST_MPEGH3DA_ESC_VAL_AUTH_SOURCE_URI_LEN,
            &cfg->authSourceURILengthMinus1))
      return FALSE;

    if (cfg->authSourceURILengthMinus1 + 1 >
        GST_MPEGH3DA_AUTH_SOURCE_URI_MAX_BYTES) {
      GST_DEBUG_OBJECT (object, "authSourceURI length %" G_GUINT64_FORMAT
          " exceeds the maximum of %d bytes",
          cfg->authSourceURILengthMinus1 + 1,
          GST_MPEGH3DA_AUTH_SOURCE_URI_MAX_BYTES);
      return FALSE;
    }

    for (i = 0; i <= cfg->authSourceURILengthMinus1; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->authSourceURI[i], 8))
        return FALSE;
    }
  }

  if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->authResilienceLevelMax, 4) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->hasAuthSequenceAUCounter, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->isFirstSequence, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->isAuthCRC, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authFrameTypes, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authMultiStreams, 1))
    return FALSE;

  GST_DEBUG_OBJECT (object, "AUTH_START: authID=%u authSequence=%u "
      "authHashType=%" G_GUINT64_FORMAT " (%s) authKeyID=%"
      G_GUINT64_FORMAT " authProvID=%" G_GUINT64_FORMAT
      " authSourceURILength=%" G_GUINT64_FORMAT
      " authResilienceLevelMax=%u hasAuthSequenceAUCounter=%u "
      "isFirstSequence=%u isAuthCRC=%u authFrameTypes=%u authMultiStreams=%u",
      cfg->authID, cfg->authSequence, cfg->authHashType,
      hash_method_name (cfg->authHashType), cfg->authKeyID, cfg->authProvID,
      cfg->authSourceURILengthMinus1 + 1, cfg->authResilienceLevelMax,
      cfg->hasAuthSequenceAUCounter, cfg->isFirstSequence, cfg->isAuthCRC,
      cfg->authFrameTypes, cfg->authMultiStreams);

  return TRUE;
}

gboolean
gst_mpegh3da_parse_AuthSig (GstObject * object, const guint8 * data,
    gsize size, GstMpegh3daAuthSig * cfg)
{
  GstBitReader br;
  guint i, sig_len;

  g_return_val_if_fail (cfg != NULL, FALSE);

  gst_bit_reader_init (&br, data, (guint) size);

  if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->authID, 8) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authSequence, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authPartialSig, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authABREnable, 2))
    return FALSE;

  if (cfg->authABREnable != 0) {
    GST_WARNING_OBJECT (object, "ABR signatures are not supported");
    return FALSE;
  }

  if (cfg->authPartialSig != 0) {
    if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->sigSegmentStart, 1) ||
        !gst_bit_reader_get_bits_uint8 (&br, &cfg->sigSegmentStop, 1) ||
        !gst_bit_reader_get_bits_uint8 (&br, &cfg->sigSegmentLengthMinus1, 4))
      return FALSE;

    sig_len = (guint) cfg->sigSegmentLengthMinus1 + 1;
    if (sig_len > GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES) {
      GST_DEBUG_OBJECT (object, "sigSegmentLength %u exceeds the maximum "
          "of %d bytes", sig_len, GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES);
      return FALSE;
    }

    for (i = 0; i < sig_len; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->sigPartial[i], 8))
        return FALSE;
    }

    GST_DEBUG_OBJECT (object, "AUTH_SIG (partial): authID=%u authSequence=%u "
        "sigSegmentStart=%u sigSegmentStop=%u sigSegmentLength=%u",
        cfg->authID, cfg->authSequence, cfg->sigSegmentStart,
        cfg->sigSegmentStop, sig_len);
  } else {
    if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->sigLengthMinus1, 6))
      return FALSE;

    sig_len = (guint) cfg->sigLengthMinus1 + 1;
    if (sig_len > GST_MPEGH3DA_SIG_COMPLETE_MAX_BYTES) {
      GST_DEBUG_OBJECT (object, "sigLength %u exceeds the maximum of %d "
          "bytes", sig_len, GST_MPEGH3DA_SIG_COMPLETE_MAX_BYTES);
      return FALSE;
    }

    for (i = 0; i < sig_len; i++) {
      if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->sigComplete[i], 8))
        return FALSE;
    }

    GST_DEBUG_OBJECT (object, "AUTH_SIG: authID=%u authSequence=%u "
        "authPartialSig=%u authABREnable=%u sigLength=%u",
        cfg->authID, cfg->authSequence, cfg->authPartialSig, cfg->authABREnable,
        sig_len);
    log_bytes_hex (object, "sigComplete", cfg->sigComplete, sig_len);
  }

  return TRUE;
}

gboolean
gst_mpegh3da_parse_AuthUUID (GstObject * object, const guint8 * data,
    gsize size, GstMpegh3daAuthUUID * cfg)
{
  GstBitReader br;
  guint i, uuid_len;

  g_return_val_if_fail (cfg != NULL, FALSE);

  gst_bit_reader_init (&br, data, (guint) size);

  if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->uuidSegmentStart, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->uuidSegmentStop, 1) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->uuidSegmentLengthMinus1, 4))
    return FALSE;

  uuid_len = (guint) cfg->uuidSegmentLengthMinus1 + 1;
  if (uuid_len > GST_MPEGH3DA_UUID_MAX_BYTES) {
    GST_DEBUG_OBJECT (object, "uuidSegmentLength %u exceeds the maximum of "
        "%d bytes", uuid_len, GST_MPEGH3DA_UUID_MAX_BYTES);
    return FALSE;
  }

  for (i = 0; i < uuid_len; i++) {
    if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->uuid[i], 8))
      return FALSE;
  }

  GST_DEBUG_OBJECT (object, "UUID: uuidSegmentStart=%u uuidSegmentStop=%u "
      "uuidSegmentLengthMinus1=%u", cfg->uuidSegmentStart, cfg->uuidSegmentStop,
      cfg->uuidSegmentLengthMinus1);
  log_bytes_hex (object, "uuid", cfg->uuid, uuid_len);

  return TRUE;
}

gboolean
gst_mpegh3da_parse_AuthTimestamp (GstObject * object, const guint8 * data,
    gsize size, GstMpegh3daAuthTimestamp * cfg)
{
  GstBitReader br;
  guint64 authTime = 0;

  g_return_val_if_fail (cfg != NULL, FALSE);

  gst_bit_reader_init (&br, data, (guint) size);

  if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->authID, 8) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authTimeType, 7) ||
      !gst_bit_reader_get_bits_uint8 (&br, &cfg->authTimeOffsetType, 1) ||
      !gst_mpegh3da_read_escaped_value (&br, GST_MPEGH3DA_ESC_VAL_AUTH_TIME,
          &authTime) ||
      !gst_bit_reader_get_bits_uint16 (&br, &cfg->authTimeOffset, 12))
    return FALSE;

  cfg->authTime = authTime;

  GST_DEBUG_OBJECT (object, "TIMESTAMP: authID=%u authTimeType=%u "
      "authTimeOffsetType=%u authTime=%" G_GUINT64_FORMAT " authTimeOffset=%u",
      cfg->authID, cfg->authTimeType, cfg->authTimeOffsetType, cfg->authTime,
      cfg->authTimeOffset);

  return TRUE;
}

gboolean
gst_mpegh3da_parse_AuthSequenceAUCounter (GstObject * object,
    const guint8 * data, gsize size, GstMpegh3daAuthSequenceAUCounter * cfg)
{
  GstBitReader br;
  guint64 counter = 0;

  g_return_val_if_fail (cfg != NULL, FALSE);

  gst_bit_reader_init (&br, data, (guint) size);

  if (!gst_bit_reader_get_bits_uint8 (&br, &cfg->authID, 8) ||
      !gst_mpegh3da_read_escaped_value (&br, GST_MPEGH3DA_ESC_VAL_AU_COUNTER,
          &counter))
    return FALSE;

  cfg->authSequenceAUCounter = (guint32) counter;

  GST_DEBUG_OBJECT (object, "AUTH_SEQUENCE_AU_COUNTER: authID=%u "
      "authSequenceAUCounter=%u", cfg->authID, cfg->authSequenceAUCounter);

  return TRUE;
}
