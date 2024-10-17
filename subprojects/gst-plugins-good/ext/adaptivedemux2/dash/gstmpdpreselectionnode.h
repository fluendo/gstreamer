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
#ifndef __GSTMPDPRESELECTIONNODE_H__
#define __GSTMPDPRESELECTIONNODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"
#include "gstmpdrepresentationbasenode.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_PRESELECTION_NODE gst_mpd_preselection_node_get_type ()
G_DECLARE_FINAL_TYPE (GstMPDPreselectionNode2, gst_mpd_preselection_node, GST, MPD_PRESELECTION_NODE, GstMPDRepresentationBaseNode)


typedef GstMPDPreselectionNode2 GstMPDPreselectionNode;
typedef GstMPDPreselectionNode2Class GstMPDPreselectionNodeClass;

struct _GstMPDPreselectionNode2
{
  GstMPDRepresentationBaseNode parent_instance;
  guint id;
  gchar *tag;
  gchar *preselectionComponents;
  gchar *codecs;
  gchar *lang;

  /* list of Accessibility DescriptorType nodes */
  GList *Accessibility;
  /* list of Role DescriptorType nodes */
  GList *Role;
  /* list of Rating DescriptorType nodes */
  GList *Rating;
  /* list of Viewpoint DescriptorType nodes */
  GList *Viewpoint;
};

GstMPDPreselectionNode * gst_mpd_preselection_node_new (void);
void gst_mpd_preselection_node_free (GstMPDPreselectionNode* self);

G_END_DECLS

#endif /* __GSTMPDPRESELECTIONNODE_H__ */
