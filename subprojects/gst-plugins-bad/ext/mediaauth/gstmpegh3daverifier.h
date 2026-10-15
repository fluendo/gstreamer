/* TODO: add copyright and license header */

#ifndef __GST_MPEGH3DAVERIFIER_H__
#define __GST_MPEGH3DAVERIFIER_H__

#include <gst/gst.h>
#include "gstmpegh3dacommon.h"
#include "gstmpegh3daverify.h"

G_BEGIN_DECLS

#define GST_TYPE_MPEGH3DAVERIFIER (gst_mpegh3daverifier_get_type ())
#define GST_MPEGH3DAVERIFIER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPEGH3DAVERIFIER, GstMpegh3DAVerifier))
#define GST_MPEGH3DAVERIFIER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPEGH3DAVERIFIER, GstMpegh3DAVerifierClass))
#define GST_IS_MPEGH3DAVERIFIER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPEGH3DAVERIFIER))
#define GST_IS_MPEGH3DAVERIFIER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPEGH3DAVERIFIER))

typedef struct _GstMpegh3DAVerifier GstMpegh3DAVerifier;
typedef struct _GstMpegh3DAVerifierClass GstMpegh3DAVerifierClass;

struct _GstMpegh3DAVerifier
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;
  guint64 buffer_count;         /* monotonic input-buffer counter for logging */

  /* properties */
  gboolean fail_on_verification_error;

  /* verification state */
  GstMpegh3daAuthData auth_data; /* current sequence data (owns hasher) */
  gboolean in_sequence;
  guint64 seq_label;            /* MHASPacketLabel of the current sequence */
};

struct _GstMpegh3DAVerifierClass
{
  GstElementClass parent_class;
};

GType gst_mpegh3daverifier_get_type (void);

G_END_DECLS

#endif /* __GST_MPEGH3DAVERIFIER_H__ */
