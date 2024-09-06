/*
 * GStreamer
 * Copyright (C) 2024 Jorge Zapata <jzapata@fluendo.com>
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

/**
 * SECTION:gstgldisplay_web
 * @short_description: Web Display connection
 * @title: GstGLDisplayWeb
 * @see_also: #GstGLDisplay
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgldisplay_web.h"

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_web_debug);
#define GST_CAT_DEFAULT gst_gl_display_web_debug

#define DEFAULT_CANVAS_SELECTOR "#canvas"

G_DEFINE_TYPE (GstGLDisplayWeb, gst_gl_display_web, GST_TYPE_GL_DISPLAY);

static void
init_debug (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gldisplayweb", 0,
        "OpenGL Web Display");
    g_once_init_leave (&_init, 1);
  }
}

static guintptr
gst_gl_display_web_get_handle (GstGLDisplay * display)
{
  return (guintptr) GST_GL_DISPLAY_WEB (display)->canvas;
}

static void
gst_gl_display_web_finalize (GObject * object)
{
  GstGLDisplayWeb *self = GST_GL_DISPLAY_WEB (object);

  g_free (self->canvas);
  G_OBJECT_CLASS (gst_gl_display_web_parent_class)->finalize (object);
}

static void
gst_gl_display_web_class_init (GstGLDisplayWebClass * klass)
{
  GST_GL_DISPLAY_CLASS (klass)->get_handle =
      GST_DEBUG_FUNCPTR (gst_gl_display_web_get_handle);

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_web_finalize;
}

static void
gst_gl_display_web_init (GstGLDisplayWeb * display_web)
{
  GstGLDisplay *display = (GstGLDisplay *) display_web;

  display->type = GST_GL_DISPLAY_TYPE_WEB;
}

/**
 * gst_gl_display_web_new:
 * @canvas (nullable): pointer to a canvas id string (or NULL)
 *
 * Create a new #GstGLDisplayWeb using the canvas id string passed or the
 * default using the WEB_DEFAULT_DISPLAY environment variable.
 *
 * Returns: (transfer full) (nullable): a new #GstGLDisplayWeb or %NULL
 *
 * Since: 1.26
 */
GstGLDisplayWeb *
gst_gl_display_web_new (gpointer canvas)
{
  GstGLDisplayWeb *ret;

  init_debug ();

  /* FIXME Use WEB_DEFAULT_DISPLAY envvar to choose the default canvas */
  ret = g_object_new (GST_TYPE_GL_DISPLAY_WEB, NULL);
  gst_object_ref_sink (ret);
  if (!canvas)
    canvas = (gpointer) DEFAULT_CANVAS_SELECTOR;
  ret->canvas = g_strdup (canvas);

  GST_DEBUG_OBJECT (ret, "GL Web Display created for canvas '%s'", (gchar *)canvas);
  return ret;
}
