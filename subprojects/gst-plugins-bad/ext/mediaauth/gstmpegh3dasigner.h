/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DASIGNER_H__
#define __GST_MPEGH3DASIGNER_H__

#include <gst/base/gstbasetransform.h>
#include "gstmpegh3dacommon.h"
#include "gstmpegh3dacrypto.h"
#include "gstmpegh3daauthpktsbuild.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEGH3DASIGNER (gst_mpegh3dasigner_get_type ())
#define GST_MPEGH3DASIGNER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPEGH3DASIGNER, GstMpegh3DASigner))
#define GST_MPEGH3DASIGNER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPEGH3DASIGNER, GstMpegh3DASignerClass))
#define GST_IS_MPEGH3DASIGNER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPEGH3DASIGNER))
#define GST_IS_MPEGH3DASIGNER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPEGH3DASIGNER))

typedef struct _GstMpegh3DASigner GstMpegh3DASigner;
typedef struct _GstMpegh3DASignerClass GstMpegh3DASignerClass;

struct _GstMpegh3DASigner
{
  GstBaseTransform parent;

  /* properties */
  GstMpegh3daHashMethod hash_method;
  guint auth_sequence_length;
  guint8 auth_id;
  gboolean au_counter;
  gboolean timestamp;

  gchar *content_uuid;          /* property: RFC 9562 UUID string */
  guint8 uuid[16];              /* parsed 16-byte UUID */
  gboolean uuid_set;

  gchar *private_key_path;      /* property: PEM RSA private key */
  gchar *public_key_uri;        /* property -> authSourceURI */
  EVP_PKEY *private_key;        /* loaded key (RSA mode only) */
  gboolean rsa_mode;            /* TRUE: RSA sign; FALSE: digest-only */

  /* authentication sequence state */
  GstMpegh3daCryptoCtx *hash;   /* digest/sign context for the current sequence */
  guint64 seq_label;            /* MHASPacketLabel of the sequence */
  gboolean have_seq_label;
  guint frame_count;            /* AUs hashed in the current sequence */
  gboolean sequence_finished;   /* previous sequence ended; start a new one */
  gboolean pending_auth_sig;    /* defer AUTH_SIG to the start of next buffer */
  guint64 buffer_count;         /* monotonic input-buffer counter for logging */
};

struct _GstMpegh3DASignerClass
{
  GstBaseTransformClass parent_class;
};

GType gst_mpegh3dasigner_get_type (void);

G_END_DECLS

#endif /* __GST_MPEGH3DASIGNER_H__ */
