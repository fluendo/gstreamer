/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DACRYPTO_H__
#define __GST_MPEGH3DACRYPTO_H__

#include <gst/gst.h>
#include <openssl/evp.h>

#include "gstmpegh3dacommon.h"

G_BEGIN_DECLS

/* OpenSSL crypto backend for MPEG-H 3D audio media authenticity.
 *
 *   - RSA PKCS#1 v1.5 signatures (EVP_DigestSign / EVP_DigestVerify),
 *   - private key: PEM PKCS#8 or PKCS#1,
 *   - public key:  PEM X.509 certificate or raw "PUBLIC KEY" PEM.
 *
 * The digest algorithm inside the RSA operation is selected by
 * GstMpegh3daHashMethod (authHashType)
 */

/* Map a hash method to the OpenSSL digest algorithm. Returns NULL when
 * unsupported. */
const EVP_MD *gst_mpegh3da_crypto_evp_md (GstMpegh3daHashMethod method);

/* Maximum signature length for a key (EVP_PKEY_size). */
gsize gst_mpegh3da_crypto_signature_size (EVP_PKEY * pkey);

/* Load a PEM private key (PKCS#8 or PKCS#1) from @path. Returns a new
 * EVP_PKEY reference, or NULL on error (logged). */
EVP_PKEY *gst_mpegh3da_crypto_load_private_key (GstObject * object,
    const gchar * path);

/* Load the public key of the first X.509 certificate in the PEM file @path. */
EVP_PKEY *gst_mpegh3da_crypto_load_public_key_from_cert (GstObject * object,
    const gchar * path);

/* Load a raw public key ("PUBLIC KEY" / "RSA PUBLIC KEY" PEM) from @path. */
EVP_PKEY *gst_mpegh3da_crypto_load_public_key (GstObject * object,
    const gchar * path);

/* ---- streaming sign / verify ---- */

/* Single EVP_MD_CTX wrapper shared by the signer and verifier paths. The mode
 * (sign vs verify) is fixed by whichever _new function created it. */
typedef struct _GstMpegh3daCryptoCtx GstMpegh3daCryptoCtx;

/* Mode of a context, fixed by the _new function that created it. */
typedef enum
{
  GST_MPEGH3DA_CRYPTO_MODE_DIGEST,
  GST_MPEGH3DA_CRYPTO_MODE_SIGN,
  GST_MPEGH3DA_CRYPTO_MODE_VERIFY,
} GstMpegh3daCryptoMode;

/* Feed bytes into a digest/sign/verify context, dispatching on its mode. */
gboolean gst_mpegh3da_crypto_update (GstMpegh3daCryptoCtx * ctx,
    const guint8 * data, gsize len);

/* Create a streaming digest/sign/verify context. @pkey is used for SIGN and
 * VERIFY modes (and may be NULL for DIGEST). Returns NULL on error (logged). */
GstMpegh3daCryptoCtx *gst_mpegh3da_crypto_new (GstObject * object,
    GstMpegh3daCryptoMode mode, EVP_PKEY * pkey,
    GstMpegh3daHashMethod method);

/* Finish signing into @sig. On entry @*sig_len is the buffer capacity; on
 * success it is set to the actual signature length. Returns FALSE on error. */
gboolean gst_mpegh3da_crypto_sign_final (GstMpegh3daCryptoCtx * ctx,
    guint8 * sig, gsize * sig_len);

/* Finish verification. Returns TRUE when the signature is valid, FALSE when
 * invalid or on error. When @error_out is non-NULL, it is set to TRUE only for
 * a real error (invalid signature sets it to FALSE). */
gboolean gst_mpegh3da_crypto_verify_final (GstMpegh3daCryptoCtx * ctx,
    const guint8 * sig, gsize sig_len, gboolean * error_out);

/* Free a context created by gst_mpegh3da_crypto_new(). */
void gst_mpegh3da_crypto_ctx_free (GstMpegh3daCryptoCtx * ctx);

/* ---- plain digest finalize / logging ---- */

gboolean gst_mpegh3da_crypto_digest_final (GstMpegh3daCryptoCtx * ctx,
    guint8 * digest, gsize * digest_size);
gsize gst_mpegh3da_crypto_digest_size (GstMpegh3daCryptoCtx * ctx);
void gst_mpegh3da_crypto_log_digest (GstObject * object,
    const guint8 * digest, gsize digest_len);

G_END_DECLS

#endif /* __GST_MPEGH3DACRYPTO_H__ */
