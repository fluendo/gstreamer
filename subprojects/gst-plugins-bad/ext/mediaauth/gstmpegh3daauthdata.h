/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DAAUTHDATA_H__
#define __GST_MPEGH3DAAUTHDATA_H__

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
 * hasher (start_hash / hash_update / hash_finish) by both the signer and the
 * verifier.
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

  /* authSequenceAUCountersValid tracking (§17.12.3): running validation of
   * the per-AU counters carried by AUTH_SEQUENCE_AU_COUNTER packets. */
  guint32 au_counter_prev;       /* last authSequenceAUCounter value seen */
  gboolean au_counter_prev_valid;/* FALSE until the first counter is seen */

  GstMpegh3daCryptoCtx *hash;   /* streaming digest/verify context for gad_bytes */

  /* RSA mode (authProvID == 0) */
  gboolean rsa_mode;
  EVP_PKEY *public_key;         /* owned reference */
  guint8 signature[GST_MPEGH3DA_MAX_AUTH_SIG_SEGMENTS *
      GST_MPEGH3DA_SIG_PARTIAL_MAX_BYTES];
  gsize signature_len;          /* bytes accumulated so far */
} GstMpegh3daAuthData;

/* Initialize @data to defaults (all-zero; flags unset). */
void gst_mpegh3da_auth_data_init (GstMpegh3daAuthData * data);

/* Free the hasher owned by @data. */
void gst_mpegh3da_auth_data_clear (GstMpegh3daAuthData * data);

/* Create the streaming digest context from auth_start.authHashType. Returns
 * FALSE if the hash method is unsupported. */
gboolean gst_mpegh3da_auth_data_start_hash (GstMpegh3daAuthData * data,
    GstObject * object);

/* Create the streaming verify context for RSA mode (authProvID == 0) using
 * @public_key. The key is referenced; the caller keeps ownership. */
gboolean gst_mpegh3da_auth_data_start_verify (GstMpegh3daAuthData * data,
    GstObject * object, EVP_PKEY * public_key);

/* Append one partial signature segment to the reassembly buffer. */
gboolean gst_mpegh3da_auth_data_append_signature (GstMpegh3daAuthData * data,
    const guint8 * seg, gsize len);

/* Validate one authSequenceAUCounter value: rejected when
 * hasAuthSequenceAUCounter is 0, otherwise required to be strictly increasing.
 * authSequenceAUCountersValid is set to TRUE when the first counter arrives
 * and cleared on any violation (§17.12.3 / §17.12.4). */
void gst_mpegh3da_auth_data_validate_au_counter (GstMpegh3daAuthData * data,
    GstObject * object, guint32 counter);

/* RSA mode finish: append the uuid field, verify the reassembled signature and
 * free the context. Returns TRUE when the signature is valid; *error_out is
 * TRUE only on a real error. */
gboolean gst_mpegh3da_auth_data_verify_finish (GstMpegh3daAuthData * data,
    GstObject * object, gboolean * error_out);

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

#endif /* __GST_MPEGH3DAAUTHDATA_H__ */
