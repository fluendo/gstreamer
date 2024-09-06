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

#ifndef __GST_GL_WINDOW_CANVAS_H__
#define __GST_GL_WINDOW_CANVAS_H__

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_WINDOW_CANVAS         (gst_gl_window_canvas_get_type())
#define GST_GL_WINDOW_CANVAS(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_GL_WINDOW_CANVAS, GstGLWindowCanvas))
#define GST_GL_WINDOW_CANVAS_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_TYPE_GL_WINDOW_CANVAS, GstGLWindowCanvasClass))
#define GST_IS_GL_WINDOW_CANVAS(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_GL_WINDOW_CANVAS))
#define GST_IS_GL_WINDOW_CANVAS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_GL_WINDOW_CANVAS))
#define GST_GL_WINDOW_CANVAS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_GL_WINDOW_CANVAS, GstGLWindowCanvas_Class))

typedef struct _GstGLWindowCanvas        GstGLWindowCanvas;
typedef struct _GstGLWindowCanvasClass   GstGLWindowCanvasClass;

struct _GstGLWindowCanvas {
  /*< private >*/
  GstGLWindow parent;

  gchar *canvas;
  gint canvas_width, canvas_height;

  gpointer _reserved[GST_PADDING];
};

struct _GstGLWindowCanvasClass {
  /*< private >*/
  GstGLWindowClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_window_canvas_get_type     (void);

GstGLWindowCanvas * gst_gl_window_canvas_new  (GstGLDisplay * display);

G_END_DECLS

#endif /* __GST_GL_WINDOW_ANDROID_H__ */

