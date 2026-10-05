/*
 * Copyright (C) 2026 Fluendo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpegh3dacommon.h"

GType
gst_mpegh3da_hash_method_get_type (void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {GST_MPEGH3DA_HASH_SHA1, "SHA-1", "sha1"},
    {GST_MPEGH3DA_HASH_SHA224, "SHA-224", "sha224"},
    {GST_MPEGH3DA_HASH_SHA256, "SHA-256", "sha256"},
    {GST_MPEGH3DA_HASH_SHA384, "SHA-384", "sha384"},
    {GST_MPEGH3DA_HASH_SHA512, "SHA-512", "sha512"},
    {0, NULL, NULL}
  };

  if (G_UNLIKELY (type == 0))
    type = g_enum_register_static ("GstMpegh3dHashMethod", values);

  return type;
}

GST_DEBUG_CATEGORY (gst_mpegh3daauth_debug);
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

/* ---- MHAS packet (Table 222) ---- */

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

  if (!gst_mpegh3da_read_escaped_value (br, 3, 8, 8, &type) ||
      !gst_mpegh3da_read_escaped_value (br, 2, 8, 32, &label) ||
      !gst_mpegh3da_read_escaped_value (br, 11, 24, 24, &length))
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

const gchar *
gst_mpegh3da_mhas_packet_type_name (guint64 type)
{
  switch (type) {
    case GST_MPEGH3DA_PACTYP_FILLDATA:
      return "FILLDATA";
    case GST_MPEGH3DA_PACTYP_MPEGH3DACFG:
      return "MPEGH3DACFG";
    case GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME:
      return "MPEGH3DAFRAME";
    case GST_MPEGH3DA_PACTYP_AUDIOSCENEINFO:
      return "AUDIOSCENEINFO";
    case GST_MPEGH3DA_PACTYP_SYNC:
      return "SYNC";
    case GST_MPEGH3DA_PACTYP_SYNCGAP:
      return "SYNCGAP";
    case GST_MPEGH3DA_PACTYP_MARKER:
      return "MARKER";
    case GST_MPEGH3DA_PACTYP_CRC16:
      return "CRC16";
    case GST_MPEGH3DA_PACTYP_CRC32:
      return "CRC32";
    case GST_MPEGH3DA_PACTYP_DESCRIPTOR:
      return "DESCRIPTOR";
    case GST_MPEGH3DA_PACTYP_USERINTERACTION:
      return "USERINTERACTION";
    case GST_MPEGH3DA_PACTYP_LOUDNESS_DRC:
      return "LOUDNESS_DRC";
    case GST_MPEGH3DA_PACTYP_BUFFERINFO:
      return "BUFFERINFO";
    case GST_MPEGH3DA_PACTYP_GLOBAL_CRC16:
      return "GLOBAL_CRC16";
    case GST_MPEGH3DA_PACTYP_GLOBAL_CRC32:
      return "GLOBAL_CRC32";
    case GST_MPEGH3DA_PACTYP_AUDIOTRUNCATION:
      return "AUDIOTRUNCATION";
    case GST_MPEGH3DA_PACTYP_GENDATA:
      return "GENDATA";
    case GST_MPEGH3DA_PACTYP_EARCON:
      return "EARCON";
    case GST_MPEGH3DA_PACTYP_PCMCONFIG:
      return "PCMCONFIG";
    case GST_MPEGH3DA_PACTYP_PCMDATA:
      return "PCMDATA";
    case GST_MPEGH3DA_PACTYP_LOUDNESS:
      return "LOUDNESS";
    case GST_MPEGH3DA_PACTYP_AUTH_START:
      return "AUTH_START";
    case GST_MPEGH3DA_PACTYP_AUTH_SIG:
      return "AUTH_SIG";
    case GST_MPEGH3DA_PACTYP_UUID:
      return "UUID";
    case GST_MPEGH3DA_PACTYP_TIMESTAMP:
      return "TIMESTAMP";
    case GST_MPEGH3DA_PACTYP_AUTH_TAG:
      return "AUTH_TAG";
    case GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER:
      return "AUTH_SEQUENCE_AU_COUNTER";
    default:
      return "UNKNOWN";
  }
}

gboolean
gst_mpegh3da_mhas_packet_type_is_excluded (guint64 type)
{
  /* Packets excluded by default from gad_bytes (ISO/IEC 23008-3, 17.12.4.1). */
  switch (type) {
    case GST_MPEGH3DA_PACTYP_SYNCGAP:
    case GST_MPEGH3DA_PACTYP_MARKER:
    case GST_MPEGH3DA_PACTYP_CRC16:
    case GST_MPEGH3DA_PACTYP_CRC32:
    case GST_MPEGH3DA_PACTYP_GLOBAL_CRC16:
    case GST_MPEGH3DA_PACTYP_GLOBAL_CRC32:
    case GST_MPEGH3DA_PACTYP_USERINTERACTION:
    case GST_MPEGH3DA_PACTYP_GENDATA:
    case GST_MPEGH3DA_PACTYP_EARCON:
    case GST_MPEGH3DA_PACTYP_PCMCONFIG:
    case GST_MPEGH3DA_PACTYP_PCMDATA:
      return TRUE;
    default:
      return FALSE;
  }
}

void
gst_mpegh3da_mhas_log_packet (const GstMpegh3daMhasPacket * pkt)
{
  g_return_if_fail (pkt != NULL);

  GST_DEBUG ("MHAS packet: type=%" G_GUINT64_FORMAT " (%s), label=%"
      G_GUINT64_FORMAT ", payload=%" G_GUINT64_FORMAT " bytes, total=%"
      G_GSIZE_FORMAT " bytes",
      pkt->type, gst_mpegh3da_mhas_packet_type_name (pkt->type),
      pkt->label, pkt->length, (gsize) (pkt->header_size + pkt->length));
}
