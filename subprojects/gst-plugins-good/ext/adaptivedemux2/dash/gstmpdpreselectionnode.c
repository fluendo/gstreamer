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
#include "gstmpdpreselectionnode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDPreselectionNode2, gst_mpd_preselection_node,
    GST_TYPE_MPD_REPRESENTATION_BASE_NODE);

/* GObject VMethods */

static void
gst_mpd_preselection_node_finalize (GObject * object)
{
  GstMPDPreselectionNode *self = GST_MPD_PRESELECTION_NODE (object);

  if (self->tag)
    xmlFree (self->tag);
  if (self->preselectionComponents)
    xmlFree (self->preselectionComponents);
  if (self->codecs)
    xmlFree (self->codecs);
  if (self->lang)
    xmlFree (self->lang);

  g_list_free_full (self->Accessibility,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);
  g_list_free_full (self->Role,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);
  g_list_free_full (self->Rating,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);
  g_list_free_full (self->Viewpoint,
      (GDestroyNotify) gst_mpd_descriptor_type_node_free);

  G_OBJECT_CLASS (gst_mpd_preselection_node_parent_class)->finalize (object);
}

static xmlNodePtr
gst_mpd_preselection_get_xml_node (GstMPDNode * node)
{
  xmlNodePtr preselection_xml_node = NULL;
  GstMPDPreselectionNode *self = GST_MPD_PRESELECTION_NODE (node);

  if (self->id)
    gst_xml_helper_set_prop_string (preselection_xml_node, "tag", self->tag);
  if (self->preselectionComponents)
    gst_xml_helper_set_prop_string (preselection_xml_node,
        "preselectionComponents", self->preselectionComponents);
  if (self->codecs)
    gst_xml_helper_set_prop_string (preselection_xml_node, "codecs",
        self->codecs);
  if (self->lang)
    gst_xml_helper_set_prop_string (preselection_xml_node, "lang", self->lang);

  g_list_foreach (self->Accessibility, gst_mpd_node_get_list_item,
      preselection_xml_node);
  g_list_foreach (self->Role, gst_mpd_node_get_list_item,
      preselection_xml_node);
  g_list_foreach (self->Rating, gst_mpd_node_get_list_item,
      preselection_xml_node);
  g_list_foreach (self->Viewpoint, gst_mpd_node_get_list_item,
      preselection_xml_node);

  return preselection_xml_node;
}

static void
gst_mpd_preselection_node_class_init (GstMPDPreselectionNodeClass * klass)
{
  GObjectClass *object_class;
  GstMPDNodeClass *m_klass;

  object_class = G_OBJECT_CLASS (klass);
  m_klass = GST_MPD_NODE_CLASS (klass);

  object_class->finalize = gst_mpd_preselection_node_finalize;

  m_klass->get_xml_node = gst_mpd_preselection_get_xml_node;
}

static void
gst_mpd_preselection_node_init (GstMPDPreselectionNode * self)
{
  self->id = 0;
  self->tag = NULL;
  self->preselectionComponents = NULL;
  self->codecs = NULL;
  self->lang = NULL;
  self->Accessibility = NULL;
  self->Role = NULL;
  self->Rating = NULL;
  self->Viewpoint = NULL;
}


GstMPDPreselectionNode *
gst_mpd_preselection_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_PRESELECTION_NODE, NULL);
}

void
gst_mpd_preselection_node_free (GstMPDPreselectionNode * self)
{
  if (self)
    gst_object_unref (self);
}
