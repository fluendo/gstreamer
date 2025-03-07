/* GStreamer
 * Copyright (C) 2025 César Fabián Orccón Chipana
 *
 * gstrusage.h: tracing module that logs resource usage stats
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

#ifndef __GST_FRAMERATE_TRACER_H__
#define __GST_FRAMERATE_TRACER_H__

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

#define GST_TYPE_FRAMERATE_TRACER \
  (gst_framerate_tracer_get_type())
#define GST_FRAMERATE_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FRAMERATE_TRACER,GstFramerateTracer))
#define GST_FRAMERATE_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FRAMERATE_TRACER,GstFramerateTracerClass))
#define GST_IS_FRAMERATE_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FRAMERATE_TRACER))
#define GST_IS_FRAMERATE_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FRAMERATE_TRACER))
#define GST_FRAMERATE_TRACER_CAST(obj) ((GstFramerateTracer *)(obj))

typedef struct _GstFramerateTracer GstFramerateTracer;
typedef struct _GstFramerateTracerClass GstFramerateTracerClass;

/**
 * GstFramerateTracer:
 *
 * Opaque #GstFramerateTracer data structure
 */
struct _GstFramerateTracer {
  GstTracer 	 parent;

  /*< private:statistics >*/
  GPid pid;
  gint frames_rendered, frames_dropped;  /* ATOMIC */
  guint64 last_frames_rendered, last_frames_dropped;

  GstElement *video_sink;
  GstClockTime start_ts;
  GstClockTime last_ts;
  GstClockTime interval_ts;
  guint data_probe_id;

  GstClockTime fps_update_interval;
  gdouble max_fps;
  gdouble min_fps;
};

struct _GstFramerateTracerClass {
  GstTracerClass parent_class;

  /* signals */
};

G_GNUC_INTERNAL GType gst_framerate_tracer_get_type (void);

G_END_DECLS

#endif /* __GST_FRAMERATE_TRACER_H__ */
