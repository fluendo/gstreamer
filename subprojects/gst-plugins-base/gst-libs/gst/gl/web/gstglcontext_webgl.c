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

#include <emscripten/html5.h>
#include <gst/gst.h>
#include <gst/gl/gl.h>
#include "../gstglcontext_private.h"

#include "gstglcontext_webgl.h"

#define GST_CAT_DEFAULT gst_gl_context_debug

struct _GstGLContextWebGLPrivate
{
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle;
};

#define gst_gl_context_webgl_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstGLContextWebGL, gst_gl_context_webgl,
    GST_TYPE_GL_CONTEXT);


static guintptr
gst_gl_context_webgl_get_gl_context (GstGLContext * context)
{
  GstGLContextWebGL *self;

  self = GST_GL_CONTEXT_WEBGL (context);
  return (guintptr)self->priv->handle;
}

static gboolean
gst_gl_context_webgl_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextWebGL *self;
  EMSCRIPTEN_RESULT result;

  self = GST_GL_CONTEXT_WEBGL (context);
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
gst_gl_context_webgl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error)
{
  GstGLContextWebGL *self;
  GstGLWindow *window = NULL;
  EmscriptenWebGLContextAttributes attrs;
  guintptr window_handle;

  self = GST_GL_CONTEXT_WEBGL (context);

  if (other_context) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Shared contexts are not allowed");
    goto failure;
  }

  window = gst_gl_context_get_window (context);
  if (!window) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Context creation requires a window");
    goto failure;
  }

#if 0
  window_handle = gst_gl_window_get_window_handle (window);
  if (!window_handle) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Window without canvas handle");
    goto failure;
  }
  /* TODO attach the canvas to the caller thread */
  //emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas");
  GST_ERROR_OBJECT (context, "Context creation for %s", (const char *)window_handle); 
  self->priv->handle = emscripten_webgl_create_context((const char *)window_handle, attrs);
#endif
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
  attrs.explicitSwapControl = EM_TRUE;
  self->priv->handle = emscripten_webgl_create_context("#canvas", &attrs);

  gst_object_unref (window);
  return TRUE;

failure:
  if (window)
    gst_object_unref (window);
  return FALSE;
}

static void
gst_gl_context_webgl_destroy_context (GstGLContext * context)
{
  GstGLContextWebGL *self;

  self = GST_GL_CONTEXT_WEBGL (context);
  emscripten_webgl_destroy_context(self->priv->handle);
  self->priv->handle = 0;
}

static gboolean
gst_gl_context_webgl_choose_format (GstGLContext * context, GError ** error)
{
  return TRUE;
}

static void gst_gl_context_webgl_swap_buffers (GstGLContext * context)
{
  emscripten_webgl_commit_frame();
}

static GstGLAPI
gst_gl_context_webgl_get_gl_api (GstGLContext * context)
{
  return GST_GL_API_GLES2;
}

static GstGLPlatform
gst_gl_context_webgl_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_WEBGL;
}

static gpointer
gst_gl_context_webgl_get_proc_address (GstGLAPI gl_api, const gchar * name)
{
  return gst_gl_context_default_get_proc_address (gl_api, name);
}

static guintptr
gst_gl_context_webgl_get_current_context (void)
{
  return (guintptr) emscripten_webgl_get_current_context();
}

static void
gst_gl_context_webgl_get_gl_platform_version (GstGLContext * context,
    gint * major, gint * minor)
{
}

static GstStructure *
gst_gl_context_webgl_get_config (GstGLContext * context)
{
  return NULL;
}

static gboolean
gst_gl_context_webgl_request_config (GstGLContext * context,
    GstStructure * config)
{
  return TRUE;
}

static GThread *
gst_gl_context_webgl_create_thread (GstGLContext * context,
    const gchar * name, GThreadFunc run)
{
  GstGLContextWebGL *self;
  GstGLWindow *window = NULL;
  GThread *thread = NULL;
  const gchar *canvas;

  self = GST_GL_CONTEXT_WEBGL (context);

  window = gst_gl_context_get_window (context);
  if (!window) {
    GST_ERROR_OBJECT (self, "Thread creation requires a window");
    goto failure;
  }

  canvas = (const gchar *)gst_gl_window_get_window_handle (window);
  canvas = "#canvas";
  if (!canvas) {
    GST_ERROR_OBJECT (self, "Thread creation requires a canvas name set");
    goto failure;
  }

  GST_ERROR_OBJECT (context, "Creating GL thread on canvas %s", canvas);
  /* Check for a window to use the canvas name */
  thread = g_thread_emscripten_new (name, canvas ? canvas : "#canvas", run, context);
  GST_ERROR_OBJECT (context, "Thread created");

failure:
  if (window) 
    gst_object_unref (window);

  return thread;
}

static void
gst_gl_context_webgl_init (GstGLContextWebGL * context)
{
  context->priv = gst_gl_context_webgl_get_instance_private (context);
}

static void
gst_gl_context_webgl_class_init (GstGLContextWebGLClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_gl_context);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_webgl_activate);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_create_context);
  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_destroy_context);
  context_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_choose_format);
  context_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_swap_buffers);

  context_class->get_gl_api = GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_gl_platform);
  context_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_proc_address);
  context_class->get_current_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_current_context);
  context_class->get_gl_platform_version =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_gl_platform_version);
  context_class->get_config = GST_DEBUG_FUNCPTR (gst_gl_context_webgl_get_config);
  context_class->request_config =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_request_config);
  context_class->create_thread =
      GST_DEBUG_FUNCPTR (gst_gl_context_webgl_create_thread);
}

GstGLContextWebGL *
gst_gl_context_webgl_new (void)
{
  GstGLContextWebGL *context;

  context = g_object_new (GST_TYPE_GL_CONTEXT_WEBGL, NULL);
  gst_object_ref_sink (context);

  return context;
}
