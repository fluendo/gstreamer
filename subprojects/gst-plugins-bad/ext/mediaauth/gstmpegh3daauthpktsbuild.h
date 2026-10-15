/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DAAUTHPKTSBUILD_H__
#define __GST_MPEGH3DAAUTHPKTSBUILD_H__

#include "gstmpegh3dacommon.h"

G_BEGIN_DECLS

/* Serialize mpegh3daAuthStart() (Table 233) into a byte-aligned payload.
 * Returns the payload length in bytes, or 0 on error. */
gsize gst_mpegh3da_build_AuthStart (const GstMpegh3daAuthStart * cfg,
    guint8 * out, gsize out_size);

/* Serialize mpegh3daAuthSig() (Table 234) into a byte-aligned payload.
 * authPartialSig selects the complete or segmented layout. */
gsize gst_mpegh3da_build_AuthSig (const GstMpegh3daAuthSig * cfg,
    guint8 * out, gsize out_size);

/* Serialize mpegh3daAuthSequenceAUCounter() (Table 238) into a payload. */
gsize gst_mpegh3da_build_AuthSequenceAUCounter (
    const GstMpegh3daAuthSequenceAUCounter * cfg, guint8 * out, gsize out_size);

/* Serialize authTimestamp() authTimeLong (Table 236) into a payload. */
gsize gst_mpegh3da_build_AuthTimestamp (const GstMpegh3daAuthTimestamp * cfg,
    guint8 * out, gsize out_size);

/* Serialize authUUID() (Table 235) into a byte-aligned payload. */
gsize gst_mpegh3da_build_AuthUUID (const GstMpegh3daAuthUUID * cfg,
    guint8 * out, gsize out_size);

G_END_DECLS

#endif /* __GST_MPEGH3DAAUTHPKTSBUILD_H__ */
