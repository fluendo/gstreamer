/*
 * GStreamer
 * Copyright (C) 2023 Jorge Zapata <jzapata@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gl/gl.h>
#include "../gstglcontext_private.h"

#include "gstglcontext_emscripten.h"

#define GST_CAT_DEFAULT gst_gl_context_debug

/* FIXME rename this to GstGLContextWebEmscripten */
/* This is not defined in any header, in emscripten it is forward referenced like this */
extern void* emscripten_GetProcAddress(const char *name);

struct _GstGLContextEmscriptenPrivate
{
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle;
};

#define gst_gl_context_emscripten_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstGLContextEmscripten, gst_gl_context_emscripten,
    GST_TYPE_GL_CONTEXT);


static guintptr
gst_gl_context_emscripten_get_gl_context (GstGLContext * context)
{
  GstGLContextEmscripten *self;

  self = GST_GL_CONTEXT_EMSCRIPTEN (context);
  return (guintptr)self->priv->handle;
}

static gboolean
gst_gl_context_emscripten_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextEmscripten *self;
  EMSCRIPTEN_RESULT result;

  self = GST_GL_CONTEXT_EMSCRIPTEN (context);
  GST_DEBUG_OBJECT (context, "Activating context");
  result = emscripten_webgl_make_context_current(self->priv->handle);
  if (!result) {
    GST_DEBUG_OBJECT (context, "Context activated");
    return TRUE;
  } else {
    GST_WARNING_OBJECT (context, "Context activation failed (%d)", result);
    return FALSE;
  }
}

static gboolean
gst_gl_context_emscripten_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error)
{
  GstGLContextEmscripten *self;
  GstGLDisplay *display = NULL;
  EmscriptenWebGLContextAttributes attrs;
  gchar *canvas;

  self = GST_GL_CONTEXT_EMSCRIPTEN (context);

  if (other_context) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Shared contexts are not allowed");
    return FALSE;
  }

  display = gst_gl_context_get_display (context);
  canvas = (gchar *) gst_gl_display_get_handle (display);
 
  emscripten_webgl_init_context_attributes (&attrs);
  attrs.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
  attrs.explicitSwapControl = EM_TRUE;
  /* comma-delimited list with # */
  GST_DEBUG_OBJECT (context, "Creating Emscripten WebGL context on %s", (gchar *)canvas);
  self->priv->handle = emscripten_webgl_create_context (canvas, &attrs);

  gst_object_unref (display);
  return TRUE;
}

static void
gst_gl_context_emscripten_destroy_context (GstGLContext * context)
{
  GstGLContextEmscripten *self;

  self = GST_GL_CONTEXT_EMSCRIPTEN (context);
  emscripten_webgl_destroy_context (self->priv->handle);
  self->priv->handle = 0;
}

static void gst_gl_context_emscripten_swap_buffers (GstGLContext * context)
{
  GST_LOG_OBJECT (context, "Swapping buffers");
  emscripten_webgl_commit_frame ();
}

static GstGLAPI
gst_gl_context_emscripten_get_gl_api (GstGLContext * context)
{
  return GST_GL_API_GLES2;
}

static GstGLPlatform
gst_gl_context_emscripten_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_EMSCRIPTEN;
}

static gpointer
gst_gl_context_emscripten_get_proc_address (GstGLAPI gl_api, const gchar * name)
{
  gpointer result;

  if (!(result = gst_gl_context_default_get_proc_address (gl_api, name))) {
    result = emscripten_GetProcAddress (name);
  }

  if (!result) {
    GST_ERROR ("Failed to get proc address for '%s'", name);
  }

  return result;
}

static guintptr
gst_gl_context_emscripten_get_current_context (void)
{
  return (guintptr) emscripten_webgl_get_current_context();
}

static GThread *
gst_gl_context_emscripten_create_thread (GstGLContext * context,
    const gchar * name, GThreadFunc run)
{
  GstGLDisplay *display;
  GThread *thread;
  gchar *canvas;

  display = gst_gl_context_get_display (context);
  canvas = (gchar *) gst_gl_display_get_handle (display);

  GST_DEBUG_OBJECT (context, "Creating GL thread on canvas %s", canvas);
  /* Check for a window to use the canvas name */
  thread = g_thread_emscripten_new (name, canvas, run, context);
  GST_DEBUG_OBJECT (context, "Thread created");

  gst_object_unref (display);
  return thread;
}

static void
gst_gl_context_emscripten_init (GstGLContextEmscripten * self)
{
  self->priv = gst_gl_context_emscripten_get_instance_private (self);
}

static void
gst_gl_context_emscripten_class_init (GstGLContextEmscriptenClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_get_gl_context);
  context_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_activate);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_create_context);
  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_destroy_context);
  context_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_swap_buffers);

  context_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_get_gl_platform);
  context_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_get_proc_address);
  context_class->get_current_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_get_current_context);
  context_class->create_thread =
      GST_DEBUG_FUNCPTR (gst_gl_context_emscripten_create_thread);
}

GstGLContextEmscripten *
gst_gl_context_emscripten_new (GstGLDisplay * display)
{
  GstGLContextEmscripten *context;

  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_WEB) == 0) {
    GST_ERROR ("Emscripten context requires a Web Display");
    return NULL;
  }

  context = g_object_new (GST_TYPE_GL_CONTEXT_EMSCRIPTEN, NULL);
  gst_object_ref_sink (context);

  return context;
}
