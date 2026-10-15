/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DAVERIFY_H__
#define __GST_MPEGH3DAVERIFY_H__

#include "gstmpegh3dacommon.h"
#include "gstmpegh3dacrypto.h"

G_BEGIN_DECLS

/* mpegh3da_GetAuthData() — media-authentication data interface for one
 * authentication sequence (ISO/IEC 23008-3:2026 Amd 1 §17.12.2, Table 297).
 *
 * Field mapping (Table 297):
 *   auth_start                      mpegh3daAuthStart()
 *   auth_sig                        mpegh3daAuthSig()
 *   auth_uuid_present / auth_uuid   authUUIDPresent / authUUID()
 *   auth_timestamp_present /
 *   auth_timestamp                  authTimestampPresent / authTimestamp()
 *   auth_sequence_au_counters_valid authSequenceAUCountersValid
 *   auth_temp_cons                  authTempCons
 *   auth_start_new                  authStartNew
 *   byte_alignment                  byte_alignment (0..7)
 *
 * The hash input (gad_bytes || uuid) is not stored: it is streamed into the
 * hasher (start_hash / hash_update / hash_finish), mirroring the signer and
 * the video DSC plugin.
 */
typedef struct _GstMpegh3daAuthData
{
  GstMpegh3daAuthStart auth_start;
  GstMpegh3daAuthSig auth_sig;

  gboolean auth_uuid_present;
  GstMpegh3daAuthUUID auth_uuid;

  gboolean auth_timestamp_present;
  GstMpegh3daAuthTimestamp auth_timestamp;

  gboolean auth_sequence_au_counters_valid;
  gboolean auth_temp_cons;
  gboolean auth_start_new;

  guint8 byte_alignment;

  GstMpegh3daCryptoCtx *hash;   /* streaming digest/verify context for gad_bytes */
} GstMpegh3daAuthData;

/* Initialize @data to defaults (all-zero; flags unset). */
void gst_mpegh3da_auth_data_init (GstMpegh3daAuthData * data);

/* Free the hasher owned by @data. */
void gst_mpegh3da_auth_data_clear (GstMpegh3daAuthData * data);

/* Create the streaming digest context from auth_start.authHashType. Returns
 * FALSE if the hash method is unsupported. */
gboolean gst_mpegh3da_auth_data_start_hash (GstMpegh3daAuthData * data,
    GstObject * object);

/* Feed @len bytes from @bytes into the hasher (gad_bytes accumulation). */
gboolean gst_mpegh3da_auth_data_hash_update (GstMpegh3daAuthData * data,
    const guint8 * bytes, gsize len);

/* Finalize: append the uuid field bytes if present, finish the digest into
 * @digest/@digest_size and free the hasher. Returns FALSE on failure. */
gboolean gst_mpegh3da_auth_data_hash_finish (GstMpegh3daAuthData * data,
    guint8 * digest, gsize * digest_size);

/* Verify the sequence signature against the recomputed @digest, according to
 * the provider (authProvID) and the signing mode (authPartialSig /
 * authABREnable) from auth_start/auth_sig. Only digest-only (authProvID=1,
 * authKeyID=0), complete, non-ABR signatures are supported. */
gboolean gst_mpegh3da_auth_data_verify_signature (GstObject * object,
    const GstMpegh3daAuthData * data, const guint8 * digest, gsize digest_size);

/* Log all fields of @data in human-readable form. */
void gst_mpegh3da_auth_data_log (GstObject * object,
    const GstMpegh3daAuthData * data);

G_END_DECLS

#endif /* __GST_MPEGH3DAVERIFY_H__ */
