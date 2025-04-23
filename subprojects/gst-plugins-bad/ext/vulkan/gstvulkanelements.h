/* GStreamer
 * Copyright (C) <2020> The GStreamer Contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_VULKAN_ELEMENTS_H__
#define __GST_VULKAN_ELEMENTS_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/vulkan/vulkan.h>

void vulkan_element_init (GstPlugin * plugin);
void gst_vulkan_create_feature_name (GstVulkanDevice * device,
                                     const gchar * type_name_default,
                                     const gchar * type_name_templ,
                                     gchar ** type_name,
                                     const gchar * feature_name_default,
                                     const gchar * feature_name_templ,
                                     gchar ** feature_name,
                                     gchar ** desc,
                                     guint * rank);

#endif /* __GST_VULKAN_ELEMENTS_H__ */
