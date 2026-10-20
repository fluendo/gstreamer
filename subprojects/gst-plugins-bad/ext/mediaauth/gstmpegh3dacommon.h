/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DACOMMON_H__
#define __GST_MPEGH3DACOMMON_H__

#include <gst/gst.h>
#include <gst/base/gstbitreader.h>
#include <gst/base/gstbitwriter.h>

G_BEGIN_DECLS

/* Hash methods (authHashType, ISO/IEC 23008-3 Table 239). Same numeric map as
 * the video DSC plugin's hash-method enum. */
typedef enum
{
  GST_MPEGH3DA_HASH_SHA1 = 0,
  GST_MPEGH3DA_HASH_SHA224 = 1,
  GST_MPEGH3DA_HASH_SHA256 = 2,
  GST_MPEGH3DA_HASH_SHA384 = 3,
  GST_MPEGH3DA_HASH_SHA512 = 4,
} GstMpegh3daHashMethod;

#define GST_TYPE_MPEGH3DA_HASH_METHOD (gst_mpegh3da_hash_method_get_type ())
GType gst_mpegh3da_hash_method_get_type (void);

/* Largest digest produced by any supported hash method (SHA-512). */
#define GST_MPEGH3DA_HASH_MAX_DIGEST_BYTES 64

/* MHAS packet types (ISO/IEC 23008-3 Table 226). */
#define GST_MPEGH3DA_PACTYP_FILLDATA                 0
#define GST_MPEGH3DA_PACTYP_MPEGH3DACFG              1
#define GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME            2
#define GST_MPEGH3DA_PACTYP_AUDIOSCENEINFO           3
#define GST_MPEGH3DA_PACTYP_SYNC                     6
#define GST_MPEGH3DA_PACTYP_SYNCGAP                  7
#define GST_MPEGH3DA_PACTYP_MARKER                   8
#define GST_MPEGH3DA_PACTYP_CRC16                    9
#define GST_MPEGH3DA_PACTYP_CRC32                    10
#define GST_MPEGH3DA_PACTYP_DESCRIPTOR               11
#define GST_MPEGH3DA_PACTYP_USERINTERACTION          12
#define GST_MPEGH3DA_PACTYP_LOUDNESS_DRC             13
#define GST_MPEGH3DA_PACTYP_BUFFERINFO               14
#define GST_MPEGH3DA_PACTYP_GLOBAL_CRC16             15
#define GST_MPEGH3DA_PACTYP_GLOBAL_CRC32             16
#define GST_MPEGH3DA_PACTYP_AUDIOTRUNCATION          17
#define GST_MPEGH3DA_PACTYP_GENDATA                  18
#define GST_MPEGH3DA_PACTYP_EARCON                   19
#define GST_MPEGH3DA_PACTYP_PCMCONFIG                20
#define GST_MPEGH3DA_PACTYP_PCMDATA                  21
#define GST_MPEGH3DA_PACTYP_LOUDNESS                 22
#define GST_MPEGH3DA_PACTYP_AUTH_START               32
#define GST_MPEGH3DA_PACTYP_AUTH_SIG                 33
#define GST_MPEGH3DA_PACTYP_UUID                     34
#define GST_MPEGH3DA_PACTYP_TIMESTAMP                35
#define GST_MPEGH3DA_PACTYP_AUTH_TAG                 36
#define GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER 37

/* escapedValue() parameter sets (Table 8C), keyed by the spec field. Each
 * expands to the (nbits1, nbits2, nbits3) triplet consumed by the read/write
 * escaped-value helpers. */
#define GST_MPEGH3DA_ESC_VAL_MHAS_TYPE    3, 8, 8
#define GST_MPEGH3DA_ESC_VAL_MHAS_LABEL   2, 8, 32
#define GST_MPEGH3DA_ESC_VAL_MHAS_LENGTH  11, 24, 24
#define GST_MPEGH3DA_ESC_VAL_AUTH_HASH    4, 8, 8
#define GST_MPEGH3DA_ESC_VAL_AUTH_KEYID   3, 8, 8
#define GST_MPEGH3DA_ESC_VAL_AUTH_PROVID  8, 8, 16
#define GST_MPEGH3DA_ESC_VAL_AU_COUNTER   8, 16, 16
#define GST_MPEGH3DA_ESC_VAL_AUTH_TIME    12, 16, 32

/* Maximum bits an escapedValue can occupy (nbits1 + nbits2 + nbits3).
 * Two-level indirection so a GST_MPEGH3DA_ESC_VAL_* triplet (e.g. "4, 8, 8")
 * expands into three arguments before being summed. */
#define GST_MPEGH3DA_ESC_VAL_MAX_BITS_IMPL(n1, n2, n3) ((n1) + (n2) + (n3))
#define GST_MPEGH3DA_ESC_VAL_MAX_BITS(triplet) \
  GST_MPEGH3DA_ESC_VAL_MAX_BITS_IMPL (triplet)

/* Fixed-width field sizes from the media authenticity syntax elements
 * (Tables 233-238). escapedValue fields use the parameter sets above. */

/* Table 233 — mpegh3daAuthStart() */
#define GST_MPEGH3DA_AUTH_ID_BITS                    8
#define GST_MPEGH3DA_AUTH_SEQUENCE_BITS              1
#define GST_MPEGH3DA_AUTH_RESILIENCE_LEVEL_MAX_BITS  4
#define GST_MPEGH3DA_HAS_AUTH_SEQUENCE_AU_COUNTER_BITS 1
#define GST_MPEGH3DA_IS_FIRST_SEQUENCE_BITS          1
#define GST_MPEGH3DA_IS_AUTH_CRC_BITS                1
#define GST_MPEGH3DA_AUTH_FRAME_TYPES_BITS           1
#define GST_MPEGH3DA_AUTH_MULTI_STREAMS_BITS         1

/* Table 234 — mpegh3daAuthSig() */
#define GST_MPEGH3DA_AUTH_PARTIAL_SIG_BITS           1
#define GST_MPEGH3DA_AUTH_ABR_ENABLE_BITS            2
#define GST_MPEGH3DA_SIG_LENGTH_MINUS1_BITS          6
#define GST_MPEGH3DA_SIG_COMPLETE_MAX_BYTES          64 /* sigLengthMinus1 max 63 -> 64 B */

/* Table 235 — authUUID() */
#define GST_MPEGH3DA_UUID_SEGMENT_START_BITS         1
#define GST_MPEGH3DA_UUID_SEGMENT_STOP_BITS          1
#define GST_MPEGH3DA_UUID_SEGMENT_LENGTH_MINUS1_BITS 4
#define GST_MPEGH3DA_UUID_MAX_BYTES                  16 /* uuidSegmentLengthMinus1 max 15 -> 16 B */

/* Table 236 — authTimestamp() (authTimeLong) */
#define GST_MPEGH3DA_AUTH_TIME_TYPE_BITS             7
#define GST_MPEGH3DA_AUTH_TIME_OFFSET_TYPE_BITS      1
#define GST_MPEGH3DA_AUTH_TIME_OFFSET_BITS           12
/* authTime base: 2025-01-01 00:00:01 UTC, in seconds since Unix epoch. */
#define GST_MPEGH3DA_AUTH_TIME_EPOCH_SEC             1735689601ULL

/* MHAS packet header worst case (type + label + length), Table 222. */
#define GST_MPEGH3DA_MHAS_HEADER_MAX_BITS \
  (GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_MHAS_TYPE) + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_MHAS_LABEL) + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_MHAS_LENGTH))
#define GST_MPEGH3DA_MHAS_HEADER_MAX_BYTES \
  ((GST_MPEGH3DA_MHAS_HEADER_MAX_BITS + 7) / 8)

/* Maximum payload sizes, derived from the spec field widths (Tables 233-238). */
#define GST_MPEGH3DA_AUTH_START_MAX_PAYLOAD_BITS \
  (GST_MPEGH3DA_AUTH_ID_BITS + GST_MPEGH3DA_AUTH_SEQUENCE_BITS + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_AUTH_HASH) + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_AUTH_KEYID) + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_AUTH_PROVID) + \
   GST_MPEGH3DA_AUTH_RESILIENCE_LEVEL_MAX_BITS + \
   GST_MPEGH3DA_HAS_AUTH_SEQUENCE_AU_COUNTER_BITS + \
   GST_MPEGH3DA_IS_FIRST_SEQUENCE_BITS + \
   GST_MPEGH3DA_IS_AUTH_CRC_BITS + \
   GST_MPEGH3DA_AUTH_FRAME_TYPES_BITS + \
   GST_MPEGH3DA_AUTH_MULTI_STREAMS_BITS)
#define GST_MPEGH3DA_AUTH_SIG_MAX_PAYLOAD_BITS \
  (GST_MPEGH3DA_AUTH_ID_BITS + GST_MPEGH3DA_AUTH_SEQUENCE_BITS + \
   GST_MPEGH3DA_AUTH_PARTIAL_SIG_BITS + GST_MPEGH3DA_AUTH_ABR_ENABLE_BITS + \
   GST_MPEGH3DA_SIG_LENGTH_MINUS1_BITS + GST_MPEGH3DA_SIG_COMPLETE_MAX_BYTES * 8)
#define GST_MPEGH3DA_UUID_MAX_PAYLOAD_BITS \
  (GST_MPEGH3DA_UUID_SEGMENT_START_BITS + \
   GST_MPEGH3DA_UUID_SEGMENT_STOP_BITS + \
   GST_MPEGH3DA_UUID_SEGMENT_LENGTH_MINUS1_BITS + GST_MPEGH3DA_UUID_MAX_BYTES * 8)
#define GST_MPEGH3DA_TIMESTAMP_MAX_PAYLOAD_BITS \
  (GST_MPEGH3DA_AUTH_ID_BITS + GST_MPEGH3DA_AUTH_TIME_TYPE_BITS + \
   GST_MPEGH3DA_AUTH_TIME_OFFSET_TYPE_BITS + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_AUTH_TIME) + \
   GST_MPEGH3DA_AUTH_TIME_OFFSET_BITS)
#define GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PAYLOAD_BITS \
  (GST_MPEGH3DA_AUTH_ID_BITS + \
   GST_MPEGH3DA_ESC_VAL_MAX_BITS (GST_MPEGH3DA_ESC_VAL_AU_COUNTER))

/* Byte-aligned maximum payload sizes. */
#define GST_MPEGH3DA_AUTH_START_MAX_PAYLOAD_BYTES \
  ((GST_MPEGH3DA_AUTH_START_MAX_PAYLOAD_BITS + 7) / 8)
#define GST_MPEGH3DA_AUTH_SIG_MAX_PAYLOAD_BYTES \
  ((GST_MPEGH3DA_AUTH_SIG_MAX_PAYLOAD_BITS + 7) / 8)
#define GST_MPEGH3DA_UUID_MAX_PAYLOAD_BYTES \
  ((GST_MPEGH3DA_UUID_MAX_PAYLOAD_BITS + 7) / 8)
#define GST_MPEGH3DA_TIMESTAMP_MAX_PAYLOAD_BYTES \
  ((GST_MPEGH3DA_TIMESTAMP_MAX_PAYLOAD_BITS + 7) / 8)
#define GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PAYLOAD_BYTES \
  ((GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PAYLOAD_BITS + 7) / 8)

/* Maximum complete packet sizes (MHAS header + payload). */
#define GST_MPEGH3DA_AUTH_START_MAX_PACKET_BYTES \
  (GST_MPEGH3DA_MHAS_HEADER_MAX_BYTES + GST_MPEGH3DA_AUTH_START_MAX_PAYLOAD_BYTES)
#define GST_MPEGH3DA_AUTH_SIG_MAX_PACKET_BYTES \
  (GST_MPEGH3DA_MHAS_HEADER_MAX_BYTES + GST_MPEGH3DA_AUTH_SIG_MAX_PAYLOAD_BYTES)
#define GST_MPEGH3DA_UUID_MAX_PACKET_BYTES \
  (GST_MPEGH3DA_MHAS_HEADER_MAX_BYTES + GST_MPEGH3DA_UUID_MAX_PAYLOAD_BYTES)
#define GST_MPEGH3DA_TIMESTAMP_MAX_PACKET_BYTES \
  (GST_MPEGH3DA_MHAS_HEADER_MAX_BYTES + GST_MPEGH3DA_TIMESTAMP_MAX_PAYLOAD_BYTES)
#define GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PACKET_BYTES \
  (GST_MPEGH3DA_MHAS_HEADER_MAX_BYTES + \
   GST_MPEGH3DA_AUTH_SEQUENCE_AU_COUNTER_MAX_PAYLOAD_BYTES)

/* escapedValue() (Table 8C), over GstBitReader. */
gboolean gst_mpegh3da_read_escaped_value (GstBitReader * br,
    guint nbits1, guint nbits2, guint nbits3, guint64 * value);

/* escapedValue() writer (Table 8C), symmetric to the reader. */
gboolean gst_mpegh3da_write_escaped_value (GstBitWriter * bw,
    guint nbits1, guint nbits2, guint nbits3, guint64 value);

/* Write a complete MHAS packet (Table 222 header + byte-aligned payload).
 * Returns the number of bytes written, or 0 on failure/overflow. */
gsize gst_mpegh3da_mhas_write_packet (guint8 * out, gsize out_size,
    guint64 type, guint64 label, const guint8 * payload, gsize payload_len);

/* MHAS packet (ISO/IEC 23008-3 Table 222). */
typedef struct _GstMpegh3daMhasPacket
{
  guint64 type;                 /* MHASPacketType */
  guint64 label;                /* MHASPacketLabel */
  guint64 length;               /* MHASPacketLength, payload bytes */
  gsize header_size;            /* header bytes consumed */
  const guint8 *payload;        /* MHASPacketPayload */
} GstMpegh3daMhasPacket;

/* mpegh3daAuthStart() — ISO/IEC 23008-3 Table 233. */
typedef struct _GstMpegh3daAuthStart
{
  guint8 authID;
  guint8 authSequence;
  guint16 authHashType;
  guint16 authKeyID;
  guint32 authProvID;
  guint8 authResilienceLevelMax;
  guint8 hasAuthSequenceAUCounter;
  guint8 isFirstSequence;
  guint8 isAuthCRC;
  guint8 authFrameTypes;
  guint8 authMultiStreams;
} GstMpegh3daAuthStart;

/* mpegh3daAuthSig() — Table 234 (authPartialSig = authABREnable = 0). */
typedef struct _GstMpegh3daAuthSig
{
  guint8 authID;
  guint8 authSequence;
  guint8 authPartialSig;
  guint8 authABREnable;
  guint8 sigLengthMinus1;
  guint8 sigComplete[GST_MPEGH3DA_SIG_COMPLETE_MAX_BYTES];
} GstMpegh3daAuthSig;

/* mpegh3daAuthSequenceAUCounter() — Table 238. */
typedef struct _GstMpegh3daAuthSequenceAUCounter
{
  guint8 authID;
  guint64 authSequenceAUCounter;
} GstMpegh3daAuthSequenceAUCounter;

/* authTimestamp() (authTimeLong) — Table 236. */
typedef struct _GstMpegh3daAuthTimestamp
{
  guint8 authID;
  guint8 authTimeType;
  guint8 authTimeOffsetType;
  guint64 authTime;
  guint16 authTimeOffset;
} GstMpegh3daAuthTimestamp;

/* authUUID() — Table 235. */
typedef struct _GstMpegh3daAuthUUID
{
  guint8 uuidSegmentStart;
  guint8 uuidSegmentStop;
  guint8 uuidSegmentLengthMinus1;
  guint8 uuid[16];
} GstMpegh3daAuthUUID;

gboolean gst_mpegh3da_mhas_parse_packet (GstBitReader * br,
    GstMpegh3daMhasPacket * pkt);
const gchar *gst_mpegh3da_mhas_packet_type_name (guint64 type);
gboolean gst_mpegh3da_mhas_packet_type_is_excluded (guint64 type);
void gst_mpegh3da_mhas_log_packet (GstObject * object,
    const GstMpegh3daMhasPacket * pkt);
void gst_mpegh3da_mhas_log_buffer (GstObject * object, const guint8 * data,
    gsize size, const gchar * title);

/* Parse an RFC 9562 UUID in canonical hyphenated form into 16 bytes. */
gboolean gst_mpegh3da_parse_uuid (const gchar * str, guint8 out[16]);

GST_DEBUG_CATEGORY_EXTERN (gst_mpegh3daauth_debug);

G_END_DECLS

#endif /* __GST_MPEGH3DACOMMON_H__ */
