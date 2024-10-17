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
#include "gstmpdtextualdescriptornode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDTextualDescriptorNode2, gst_mpd_textual_descriptor_node,
    GST_TYPE_MPD_NODE);

/* GObject VMethods */

static void
gst_mpd_textual_descriptor_node_finalize (GObject * object)
{
  GstMPDTextualDescriptorNode *self = GST_MPD_TEXTUAL_DESCRIPTOR_NODE (object);

  if (self->node_name)
    xmlFree (self->node_name);
  if (self->lang)
    xmlFree (self->lang);
  if (self->content)
    xmlFree (self->content);

  G_OBJECT_CLASS (gst_mpd_textual_descriptor_node_parent_class)->finalize
      (object);
}

/* Base class */

static xmlNodePtr
gst_mpd_textual_descriptor_get_xml_node (GstMPDNode * node)
{
  xmlNodePtr textual_descriptor_xml_node = NULL;
  GstMPDTextualDescriptorNode *self = GST_MPD_TEXTUAL_DESCRIPTOR_NODE (node);

  textual_descriptor_xml_node = xmlNewNode (NULL, (xmlChar *) self->node_name);

  gst_xml_helper_set_prop_uint (textual_descriptor_xml_node, "id", self->id);
  gst_xml_helper_set_prop_string (textual_descriptor_xml_node, "lang",
      self->lang);
  gst_xml_helper_set_prop_string (textual_descriptor_xml_node, "content",
      self->content);

  return textual_descriptor_xml_node;
}

static void
gst_mpd_textual_descriptor_node_class_init (GstMPDTextualDescriptorNodeClass *
    klass)
{
  GObjectClass *object_class;
  GstMPDNodeClass *m_klass;

  object_class = G_OBJECT_CLASS (klass);
  m_klass = GST_MPD_NODE_CLASS (klass);

  object_class->finalize = gst_mpd_textual_descriptor_node_finalize;

  m_klass->get_xml_node = gst_mpd_textual_descriptor_get_xml_node;
}

static void
gst_mpd_textual_descriptor_node_init (GstMPDTextualDescriptorNode * self)
{
  self->node_name = NULL;
  self->id = 0;
  self->lang = NULL;
  self->content = NULL;
}

GstMPDTextualDescriptorNode *
gst_mpd_textual_descriptor_node_new (const gchar * name)
{
  GstMPDTextualDescriptorNode *self =
      g_object_new (GST_TYPE_MPD_TEXTUAL_DESCRIPTOR_NODE, NULL);
  self->node_name = g_strdup (name);
  return self;
}

void
gst_mpd_textual_descriptor_node_free (GstMPDTextualDescriptorNode * self)
{
  if (self)
    gst_object_unref (self);
}
