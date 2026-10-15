/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DAHASH_H__
#define __GST_MPEGH3DAHASH_H__

#include <gst/gst.h>
#include "gstmpegh3dacommon.h"

G_BEGIN_DECLS

/* Incremental digest hasher (SHA-1/224/256/384/512), OpenSSL EVP digest
 * backed. Shares the hash-method -> digest mapping with the RSA sign/verify
 * backend in gstmpegh3dacrypto.c. */
typedef struct _GstMpegh3daHash GstMpegh3daHash;

GstMpegh3daHash *gst_mpegh3da_hash_new (GstMpegh3daHashMethod method);
void gst_mpegh3da_hash_free (GstMpegh3daHash * hash);
gboolean gst_mpegh3da_hash_update (GstMpegh3daHash * hash,
    const guint8 * data, gsize size);
gboolean gst_mpegh3da_hash_finish (GstMpegh3daHash * hash, guint8 * digest,
    gsize * digest_size);
gsize gst_mpegh3da_hash_get_digest_size (GstMpegh3daHash * hash);
void gst_mpegh3da_hash_log_digest (GstObject * object, const guint8 * digest,
    gsize digest_len);

G_END_DECLS

#endif /* __GST_MPEGH3DAHASH_H__ */
