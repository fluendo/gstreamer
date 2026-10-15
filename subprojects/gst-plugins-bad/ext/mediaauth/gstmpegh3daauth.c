/* TODO: add copyright and license header */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpegh3dasigner.h"
#include "gstmpegh3daverifier.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_mpegh3daauth_debug, "mpegh3daauth", 0,
      "MPEG-H 3D Audio media authenticity signer/verifier");

  if (!gst_element_register (plugin, "mpegh3dasigner", GST_RANK_NONE,
          GST_TYPE_MPEGH3DASIGNER))
    return FALSE;

  return gst_element_register (plugin, "mpegh3daverifier", GST_RANK_NONE,
      GST_TYPE_MPEGH3DAVERIFIER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpegh3daauth,
    "MPEG-H 3D Audio media authenticity signer/verifier",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
