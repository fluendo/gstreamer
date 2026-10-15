/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DAPARSE_H__
#define __GST_MPEGH3DAPARSE_H__

#include "gstmpegh3dacommon.h"

G_BEGIN_DECLS

/* escapedValue() (Table 8C), over GstBitReader. */
gboolean gst_mpegh3da_read_escaped_value (GstBitReader * br,
    guint nbits1, guint nbits2, guint nbits3, guint64 * value);

/* Parse one MHAS packet (Table 222) from a byte-aligned bit reader. */
gboolean gst_mpegh3da_mhas_parse_packet (GstBitReader * br,
    GstMpegh3daMhasPacket * pkt);

/* Log every MHAS packet (header + payload length) in a byte buffer. */
void gst_mpegh3da_mhas_log_buffer (GstObject * object, const guint8 * data,
    gsize size, const gchar * title);

/* Auth payload parsers (Tables 233-238). Each decodes a byte-aligned payload
 * and logs the parsed fields in human-readable form via @object. */
gboolean gst_mpegh3da_parse_AuthStart (GstObject * object,
    const guint8 * data, gsize size, GstMpegh3daAuthStart * cfg);
gboolean gst_mpegh3da_parse_AuthSig (GstObject * object,
    const guint8 * data, gsize size, GstMpegh3daAuthSig * cfg);
gboolean gst_mpegh3da_parse_AuthUUID (GstObject * object,
    const guint8 * data, gsize size, GstMpegh3daAuthUUID * cfg);
gboolean gst_mpegh3da_parse_AuthTimestamp (GstObject * object,
    const guint8 * data, gsize size, GstMpegh3daAuthTimestamp * cfg);
gboolean gst_mpegh3da_parse_AuthSequenceAUCounter (GstObject * object,
    const guint8 * data, gsize size, GstMpegh3daAuthSequenceAUCounter * cfg);

G_END_DECLS

#endif /* __GST_MPEGH3DAPARSE_H__ */
