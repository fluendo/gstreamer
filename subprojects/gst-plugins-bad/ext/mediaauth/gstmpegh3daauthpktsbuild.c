/* TODO: add copyright and license header */

#include "gstmpegh3daauthpktsbuild.h"

/* Serialize mpegh3daAuthStart() (Table 233) into a byte-aligned payload.
 * Returns the payload length in bytes, or 0 on error. */
gsize
gst_mpegh3da_build_AuthStart (const GstMpegh3daAuthStart * cfg,
    guint8 * out, gsize out_size)
{
  GstBitWriter bw;
  guint size_bits;

  g_return_val_if_fail (cfg != NULL, 0);
  g_return_val_if_fail (out != NULL, 0);

  gst_bit_writer_init_with_data (&bw, out, (guint) out_size, FALSE);

  if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->authID, 8) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authSequence, 1) ||
      !gst_mpegh3da_write_escaped_value (&bw, GST_MPEGH3DA_ESC_VAL_AUTH_HASH,
          cfg->authHashType) ||
      !gst_mpegh3da_write_escaped_value (&bw, GST_MPEGH3DA_ESC_VAL_AUTH_KEYID,
          cfg->authKeyID) ||
      !gst_mpegh3da_write_escaped_value (&bw, GST_MPEGH3DA_ESC_VAL_AUTH_PROVID,
          cfg->authProvID) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authResilienceLevelMax, 4) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->hasAuthSequenceAUCounter, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->isFirstSequence, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->isAuthCRC, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authFrameTypes, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authMultiStreams, 1))
    return 0;

  gst_bit_writer_align_bytes (&bw, 0);
  size_bits = gst_bit_writer_get_size (&bw);

  return size_bits / 8;
}

/* Serialize mpegh3daAuthSig() (Table 234) into a byte-aligned payload. */
gsize
gst_mpegh3da_build_AuthSig (const GstMpegh3daAuthSig * cfg,
    guint8 * out, gsize out_size)
{
  GstBitWriter bw;
  guint size_bits, i;

  g_return_val_if_fail (cfg != NULL, 0);
  g_return_val_if_fail (out != NULL, 0);

  gst_bit_writer_init_with_data (&bw, out, (guint) out_size, FALSE);

  if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->authID, 8) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authSequence, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authPartialSig, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authABREnable, 2) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->sigLengthMinus1, 6))
    return 0;

  /* sigComplete starts at a non-byte-aligned offset; write byte-wise. */
  for (i = 0; i <= cfg->sigLengthMinus1; i++) {
    if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->sigComplete[i], 8))
      return 0;
  }

  gst_bit_writer_align_bytes (&bw, 0);
  size_bits = gst_bit_writer_get_size (&bw);

  return size_bits / 8;
}

/* Serialize mpegh3daAuthSequenceAUCounter() (Table 238) into a payload. */
gsize
gst_mpegh3da_build_AuthSequenceAUCounter (const GstMpegh3daAuthSequenceAUCounter
    * cfg, guint8 * out, gsize out_size)
{
  GstBitWriter bw;
  guint size_bits;

  g_return_val_if_fail (cfg != NULL, 0);
  g_return_val_if_fail (out != NULL, 0);

  gst_bit_writer_init_with_data (&bw, out, (guint) out_size, FALSE);

  if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->authID, 8) ||
      !gst_mpegh3da_write_escaped_value (&bw, GST_MPEGH3DA_ESC_VAL_AU_COUNTER,
          cfg->authSequenceAUCounter))
    return 0;

  gst_bit_writer_align_bytes (&bw, 0);
  size_bits = gst_bit_writer_get_size (&bw);

  return size_bits / 8;
}

/* Serialize authTimestamp() authTimeLong (Table 236) into a payload. */
gsize
gst_mpegh3da_build_AuthTimestamp (const GstMpegh3daAuthTimestamp * cfg,
    guint8 * out, gsize out_size)
{
  GstBitWriter bw;
  guint size_bits;

  g_return_val_if_fail (cfg != NULL, 0);
  g_return_val_if_fail (out != NULL, 0);

  gst_bit_writer_init_with_data (&bw, out, (guint) out_size, FALSE);

  if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->authID, 8) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authTimeType, 7) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->authTimeOffsetType, 1) ||
      !gst_mpegh3da_write_escaped_value (&bw, GST_MPEGH3DA_ESC_VAL_AUTH_TIME,
          cfg->authTime) ||
      !gst_bit_writer_put_bits_uint16 (&bw, cfg->authTimeOffset, 12))
    return 0;

  gst_bit_writer_align_bytes (&bw, 0);
  size_bits = gst_bit_writer_get_size (&bw);

  return size_bits / 8;
}

/* Serialize authUUID() (Table 235) into a byte-aligned payload. */
gsize
gst_mpegh3da_build_AuthUUID (const GstMpegh3daAuthUUID * cfg,
    guint8 * out, gsize out_size)
{
  GstBitWriter bw;
  guint size_bits, i;

  g_return_val_if_fail (cfg != NULL, 0);
  g_return_val_if_fail (out != NULL, 0);

  gst_bit_writer_init_with_data (&bw, out, (guint) out_size, FALSE);

  if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->uuidSegmentStart, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->uuidSegmentStop, 1) ||
      !gst_bit_writer_put_bits_uint8 (&bw, cfg->uuidSegmentLengthMinus1, 4))
    return 0;

  for (i = 0; i <= cfg->uuidSegmentLengthMinus1; i++) {
    if (!gst_bit_writer_put_bits_uint8 (&bw, cfg->uuid[i], 8))
      return 0;
  }

  gst_bit_writer_align_bytes (&bw, 0);
  size_bits = gst_bit_writer_get_size (&bw);

  return size_bits / 8;
}
