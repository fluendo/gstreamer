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

#ifndef __GST_GL_CONTEXT_WEBGL_H__
#define __GST_GL_CONTEXT_WEBGL_H__

#include <gst/gl/gstgl_fwd.h>
#include <gst/gl/gstglcontext.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_CONTEXT_WEBGL         (gst_gl_context_webgl_get_type())
G_GNUC_INTERNAL GType gst_gl_context_webgl_get_type (void);

#define GST_GL_CONTEXT_WEBGL(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_GL_CONTEXT_WEBGL, GstGLContextWebGL))
#define GST_GL_CONTEXT_WEBGL_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_TYPE_GL_CONTEXT_WEBGL, GstGLContextWebGLClass))
#define GST_IS_GL_CONTEXT_WEBGL(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_GL_CONTEXT_WEBGL))
#define GST_IS_GL_CONTEXT_WEBGL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_GL_CONTEXT_WEBGL))
#define GST_GL_CONTEXT_WEBGL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_GL_CONTEXT_WEBGL, GstGLContextWebGL_Class))

typedef struct _GstGLContextWebGL        GstGLContextWebGL;
typedef struct _GstGLContextWebGLClass   GstGLContextWebGLClass;
typedef struct _GstGLContextWebGLPrivate GstGLContextWebGLPrivate;

struct _GstGLContextWebGL {
  /*< private >*/
  GstGLContext parent;

  GstGLContextWebGLPrivate *priv;

  gpointer _reserved[GST_PADDING];
};

struct _GstGLContextWebGLClass {
  /*< private >*/
  GstGLContextClass parent_class;

  gpointer _reserved[GST_PADDING];
};

G_GNUC_INTERNAL
GstGLContextWebGL *   gst_gl_context_webgl_new                  (void);

G_END_DECLS

#endif /* __GST_GL_CONTEXT_H__ */

