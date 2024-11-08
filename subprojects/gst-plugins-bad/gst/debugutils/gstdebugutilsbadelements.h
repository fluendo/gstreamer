/* GStreamer
 * Copyright (C) <2020> Julian Bouzas <julian.bouzas@collabora.com>
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


#ifndef __GST_DEBUGUTILSBAD_ELEMENT_H__
#define __GST_DEBUGUTILSBAD_ELEMENT_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

GST_ELEMENT_REGISTER_DECLARE (checksumsink);
GST_ELEMENT_REGISTER_DECLARE (chopmydata);
GST_ELEMENT_REGISTER_DECLARE (clockselect);
GST_ELEMENT_REGISTER_DECLARE (compare);
GST_ELEMENT_REGISTER_DECLARE (debugspy);
GST_ELEMENT_REGISTER_DECLARE (errorignore);
#ifndef __EMSCRIPTEN__
GST_ELEMENT_REGISTER_DECLARE (fakeaudiosink);
GST_ELEMENT_REGISTER_DECLARE (fakevideosink);
#endif
GST_ELEMENT_REGISTER_DECLARE (fpsdisplaysink);
GST_ELEMENT_REGISTER_DECLARE (testsrcbin);
#ifndef __EMSCRIPTEN__
GST_ELEMENT_REGISTER_DECLARE (videocodectestsink);
#endif
GST_ELEMENT_REGISTER_DECLARE (watchdog);


#endif /* __GST_DEBUGUTILSBAD_PLUGIN_H__ */
