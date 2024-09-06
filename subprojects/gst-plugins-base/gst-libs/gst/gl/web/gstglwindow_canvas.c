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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <emscripten/html5.h>
#include <gst/gst.h>
#include <gst/gl/gl.h>
#include "gstglcontext_emscripten.h"
#include "gstglwindow_canvas.h"
#include "../gstglwindow_private.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_canvas_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowCanvas, gst_gl_window_canvas,
    GST_TYPE_GL_WINDOW);

static EM_BOOL
gst_gl_window_canvas_mouse_cb (int event_type,
    const EmscriptenMouseEvent *event, void *data)
{
  GstGLWindow *window = GST_GL_WINDOW (data);
  GstGLWindowCanvas *self = GST_GL_WINDOW_CANVAS (window);

  GST_DEBUG_OBJECT (self, "Mouse event %d received", event_type);
  switch (event_type) {
    case EMSCRIPTEN_EVENT_MOUSEMOVE:
      GST_LOG_OBJECT (window, "Mouse move to %d %d", event->targetX,
          event->targetY);
      gst_gl_window_send_mouse_event (window, "mouse-move", 0, event->targetX,
          event->targetY);
      break;
    case EMSCRIPTEN_EVENT_MOUSEDOWN:
      GST_LOG_OBJECT (window, "Mouse down with button %d at %d %d",
          event->button, event->targetX, event->targetY);
      gst_gl_window_send_mouse_event (window, "mouse-button-press",
          event->button, event->targetX, event->targetY);
      break;
    case EMSCRIPTEN_EVENT_MOUSEUP:
      GST_LOG_OBJECT (window, "Mouse up with button %d at %d %d",
          event->button, event->targetX, event->targetY);
      gst_gl_window_send_mouse_event (window, "mouse-button-release",
          event->button, event->targetX, event->targetY);
      break;
    default:
      GST_WARNING_OBJECT (self, "Event %d not handled", event_type);
      break;
  }
  return EM_TRUE;
}

static void
gst_gl_window_canvas_init (GstGLWindowCanvas * self)
{
}

static guintptr
gst_gl_window_canvas_get_window_handle (GstGLWindow * window)
{
  GstGLWindowCanvas *self = GST_GL_WINDOW_CANVAS (window);

  return (guintptr) self->canvas;
}

static void
gst_gl_window_canvas_draw_cb (gpointer data)
{
  GstGLWindowCanvas *self = data;
  GstGLWindow *window = GST_GL_WINDOW (self);
  GstGLContext *context = gst_gl_window_get_context (window);
  guintptr context_handle;
  gint width, height;
  guint window_width, window_height;

  /* Given that we can not register for a callback of when the canvas
   * sized changed, we need to ask for it ourselves
   */

  gst_gl_window_get_surface_dimensions (window, &window_width,
      &window_height);
  context_handle = gst_gl_context_get_gl_context (context);
  if (!emscripten_webgl_get_drawing_buffer_size (context_handle,
          &width, &height)
      && (window->queue_resize || width != window_width
          || height != window_height)) {
    GST_DEBUG_OBJECT (window, "Resizing to %dx%d from %dx%d", width, height,
        window_width, window_height);
    gst_gl_window_resize (window, width, height);
  }

  if (window->draw)
    window->draw (window->draw_data);

  gst_gl_context_swap_buffers (context);

  gst_object_unref (context);
}

static void
gst_gl_window_canvas_draw (GstGLWindow * window)
{
  gst_gl_window_send_message (window,
      gst_gl_window_canvas_draw_cb, window);
}

static void
gst_gl_window_canvas_finalize (GObject * object)
{
  GstGLWindowCanvas *self = GST_GL_WINDOW_CANVAS (object);

  g_free (self->canvas);
  G_OBJECT_CLASS (gst_gl_window_canvas_parent_class)->finalize (object);
}

static void
gst_gl_window_canvas_class_init (GstGLWindowCanvasClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_canvas_get_window_handle);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_canvas_draw);
  G_OBJECT_CLASS (klass)->finalize = gst_gl_window_canvas_finalize;
}

/* Must be called in the gl thread */
GstGLWindowCanvas *
gst_gl_window_canvas_new (GstGLDisplay * display)
{
  GstGLWindowCanvas *self;

  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_WEB) == 0) {
    GST_ERROR ("Wrong display type, this requires a Web display");
    return NULL;
  }

  self = g_object_new (GST_TYPE_GL_WINDOW_CANVAS, NULL);
  self->canvas = g_strdup ((gchar *)gst_gl_display_get_handle (display));
  /* This callbacks are being registered on the calling thread. The window
   * creation usually happens on the main thread. Compiling with
   * PROXY_WITH_PTHREAD should make this be registered on the proxy thread
   * but we still recieve the 'blocking the main thread' warnings
   */
  emscripten_set_click_callback(self->canvas, self, FALSE, gst_gl_window_canvas_mouse_cb);
  emscripten_set_mouseup_callback(self->canvas, self, FALSE, gst_gl_window_canvas_mouse_cb);
  emscripten_set_mousedown_callback(self->canvas, self, FALSE, gst_gl_window_canvas_mouse_cb);
  emscripten_set_mousemove_callback(self->canvas, self, FALSE, gst_gl_window_canvas_mouse_cb);
  /* TODO pending mouseenter, mouseleave, dblclick */
  gst_object_ref_sink (self);

  return self;
}
