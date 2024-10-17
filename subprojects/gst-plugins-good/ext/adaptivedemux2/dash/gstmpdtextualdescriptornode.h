/* DASH MPD parsing library
 *
 * Copyright (C) 2024 Fluendo S.A. <contact@fluendo.com>
 *   Authors: Diego Nieto <dnieto@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef __GSTMPDTEXTUALDESCRIPTORNODE_H__
#define __GSTMPDTEXTUALDESCRIPTORNODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_TEXTUAL_DESCRIPTOR_NODE gst_mpd_textual_descriptor_node_get_type ()
G_DECLARE_FINAL_TYPE (GstMPDTextualDescriptorNode2, gst_mpd_textual_descriptor_node, GST, MPD_TEXTUAL_DESCRIPTOR_NODE, GstMPDNode)

typedef GstMPDTextualDescriptorNode2 GstMPDTextualDescriptorNode;
typedef GstMPDTextualDescriptorNode2Class GstMPDTextualDescriptorNodeClass;

struct _GstMPDTextualDescriptorNode2
{
  GstObject parent_instance;
  gchar *node_name;
  guint id;
  gchar *lang; /* LangVectorType RFC 5646 */
  gchar *content;
};

GstMPDTextualDescriptorNode * gst_mpd_textual_descriptor_node_new (const gchar* name);
void gst_mpd_textual_descriptor_node_free (GstMPDTextualDescriptorNode* self);

G_END_DECLS

#endif /* __GSTMPDTEXTUALDESCRIPTORNODE_H__ */
