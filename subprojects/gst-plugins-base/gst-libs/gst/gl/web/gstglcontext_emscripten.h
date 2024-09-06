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

#ifndef __GST_GL_CONTEXT_EMSCRIPTEN_H__
#define __GST_GL_CONTEXT_EMSCRIPTEN_H__

#include <gst/gl/gstgl_fwd.h>
#include <gst/gl/gstglcontext.h>
#include <emscripten/html5.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_CONTEXT_EMSCRIPTEN         (gst_gl_context_emscripten_get_type())
G_GNUC_INTERNAL GType gst_gl_context_emscripten_get_type (void);

#define GST_GL_CONTEXT_EMSCRIPTEN(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_GL_CONTEXT_EMSCRIPTEN, GstGLContextEmscripten))
#define GST_GL_CONTEXT_EMSCRIPTEN_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_TYPE_GL_CONTEXT_EMSCRIPTEN, GstGLContextEmscriptenClass))
#define GST_IS_GL_CONTEXT_EMSCRIPTEN(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_GL_CONTEXT_EMSCRIPTEN))
#define GST_IS_GL_CONTEXT_EMSCRIPTEN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_GL_CONTEXT_EMSCRIPTEN))
#define GST_GL_CONTEXT_EMSCRIPTEN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_GL_CONTEXT_EMSCRIPTEN, GstGLContextEmscripten_Class))

typedef struct _GstGLContextEmscripten        GstGLContextEmscripten;
typedef struct _GstGLContextEmscriptenClass   GstGLContextEmscriptenClass;
typedef struct _GstGLContextEmscriptenPrivate GstGLContextEmscriptenPrivate;

struct _GstGLContextEmscripten {
  /*< private >*/
  GstGLContext parent;

  GstGLContextEmscriptenPrivate *priv;

  gpointer _reserved[GST_PADDING];
};

struct _GstGLContextEmscriptenClass {
  /*< private >*/
  GstGLContextClass parent_class;

  gpointer _reserved[GST_PADDING];
};

G_GNUC_INTERNAL
GstGLContextEmscripten *   gst_gl_context_emscripten_new                  (GstGLDisplay * display);

G_END_DECLS

#endif /* __GST_GL_CONTEXT_EMSCRIPTEN_H__ */

