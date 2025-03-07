/* GStreamer
 * Copyright (c) 2006 Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (c) 2009 Nokia Corporation <multimedia@maemo.org>
 * Copyright (C) 2025 Fluendo S.A.
 *   @authors: César Fabián Orccón Chipana
 *
 * gstframerate.c: tracing module that logs processing framerate stats
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
 * SECTION:tracer-framerate
 * @short_description: log processing framerate stats
 *
 * A tracing module that determines the pipeline framerate and buffer
 * drop rate. This elements supports tracing the entire pipeline framerate.
 *
 * ```
 * GST_TRACERS="framerate" GST_DEBUG=GST_TRACER:7 ./...
 * ```
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/base/gstbasesink.h>
#include <unistd.h>
#include "gstframerate.h"

GST_DEBUG_CATEGORY_STATIC (gst_framerate_debug);
#define GST_CAT_DEFAULT gst_framerate_debug

#if !GST_CHECK_VERSION(1, 26, 0)
#define gst_structure_new_static_str gst_structure_new
#endif

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_framerate_debug, "framerate", 0, "framerate tracer");
#define gst_framerate_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstFramerateTracer, gst_framerate_tracer,
    GST_TYPE_TRACER, _do_init);

#define DEFAULT_FPS_UPDATE_INTERVAL_MS 500      /* 500 ms */

static GstTracerRecord *tr_framerate;

static GstElement *
get_real_pad_parent (GstPad * pad)
{
  GstObject *parent;

  if (!pad)
    return NULL;

  parent = gst_object_get_parent (GST_OBJECT_CAST (pad));

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD (parent)) {
    GstObject *tmp;
    pad = GST_PAD_CAST (parent);
    tmp = gst_object_get_parent (GST_OBJECT_CAST (pad));
    gst_object_unref (parent);
    parent = tmp;
  }
  return GST_ELEMENT_CAST (parent);
}

/* FIXME: Undefined behavior if multiple video sinks exist. */
static gboolean
is_target_video_sink (GstElement * element)
{
  const gchar *klass;

  if (!GST_IS_ELEMENT (element))
    return FALSE;

  klass = gst_element_class_get_metadata (GST_ELEMENT_GET_CLASS (element),
      GST_ELEMENT_METADATA_KLASS);

  return GST_IS_BASE_SINK (element) &&
      GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_FLAG_SINK) && klass &&
      g_strstr_len (klass, -1, "Video") && g_strstr_len (klass, -1, "Sink");
}

static gboolean
gst_framerate_tracer_display_current_fps (GstFramerateTracer * self,
    GstClockTime current_ts)
{
  guint64 frames_rendered, frames_dropped;
  gdouble rr, dr, average_fps;
  gdouble time_diff, time_elapsed;

  frames_rendered = g_atomic_int_get (&self->frames_rendered);
  frames_dropped = g_atomic_int_get (&self->frames_dropped);

  if ((frames_rendered + frames_dropped) == 0) {
    /* in case timer fired and we didn't yet get any QOS events */
    return TRUE;
  }

  time_diff = (gdouble) (current_ts - self->last_ts) / GST_SECOND;
  time_elapsed = (gdouble) (current_ts - self->start_ts) / GST_SECOND;

  rr = (gdouble) (frames_rendered - self->last_frames_rendered) / time_diff;
  dr = (gdouble) (frames_dropped - self->last_frames_dropped) / time_diff;

  average_fps = (gdouble) frames_rendered / time_elapsed;

  if (self->max_fps == -1 || rr > self->max_fps) {
    self->max_fps = rr;
    GST_DEBUG_OBJECT (self, "Updated max-fps to %f", rr);
  }
  if (self->min_fps == -1 || rr < self->min_fps) {
    self->min_fps = rr;
    GST_DEBUG_OBJECT (self, "Updated min-fps to %f", rr);
  }

  /* Display on a single line to make it easier to read and import
   * into, for example, excel..  note: it would be nice to show
   * timestamp too.. need to check if there is a sane way to log
   * timestamp of last rendered buffer, so we could correlate dips
   * in framerate to certain positions in the stream.
   */
  if (dr == 0.0) {
    gst_tracer_record_log (tr_framerate, self->pid, current_ts, frames_rendered,
        frames_dropped, average_fps, -1.0f, -1.0f);
  } else {
    gst_tracer_record_log (tr_framerate, self->pid, current_ts, frames_rendered,
        frames_dropped, -1.0f, rr, dr);
  }

  self->last_frames_rendered = frames_rendered;
  self->last_frames_dropped = frames_dropped;
  self->last_ts = current_ts;

  return TRUE;
}

static void
gst_framerate_tracer_calculate_framerate (GstFramerateTracer * self,
    GstClockTime ts)
{
  g_atomic_int_inc (&self->frames_rendered);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (self->start_ts))) {
    self->interval_ts = self->last_ts = self->start_ts = ts;
  }
  if (GST_CLOCK_DIFF (self->interval_ts, ts) > self->fps_update_interval) {
    gst_framerate_tracer_display_current_fps (self, ts);
    self->interval_ts = ts;
  }
}

static void
do_push_buffer_pre (GstTracer * tracer, GstClockTime ts, GstPad * pad,
    GstBuffer * buf)
{
  GstPad *peer_pad;
  GstElement *element;

  peer_pad = gst_pad_get_peer (pad);
  if (!peer_pad)
    return;

  element = get_real_pad_parent (peer_pad);
  if (!is_target_video_sink (element))
    goto beach;

  gst_framerate_tracer_calculate_framerate (GST_FRAMERATE_TRACER (tracer), ts);

beach:
  if (element)
    gst_object_unref (element);
  if (peer_pad)
    gst_object_unref (peer_pad);
}

/* tracer class */

static void
gst_framerate_tracer_class_init (GstFramerateTracerClass * klass)
{
  /* announce trace formats */
  /* *INDENT-OFF* */
  tr_framerate = gst_tracer_record_new ("framerate.class",
     "pid", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_INT,
          "description", G_TYPE_STRING, "PID of the process running the pipeline",
          "min", G_TYPE_INT, 0,
          "max", G_TYPE_INT, G_MAXINT,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "ts when the framerate has been logged",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "rendered", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "number of rendered frames",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "dropped", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "number of dropped frames",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "average", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_FLOAT,
          "description", G_TYPE_STRING, "Average frame rate",
          NULL),
      "current", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_FLOAT,
          "description", G_TYPE_STRING, "Number of frames rendered per time unit",
          NULL),
      "drop rate", GST_TYPE_STRUCTURE, gst_structure_new_static_str ("value",
          "type", G_TYPE_GTYPE, G_TYPE_FLOAT,
          "description", G_TYPE_STRING, "Number of frames dropped per time unit",
          NULL),
      NULL);
  /* *INDENT-ON* */

  GST_OBJECT_FLAG_SET (tr_framerate, GST_OBJECT_FLAG_MAY_BE_LEAKED);
}

static void
do_element_post_message_pre (GstFramerateTracer * self, GstClockTime ts,
    GstElement * element, GstMessage * message)
{
  if (!is_target_video_sink (element))
    return;


  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_QOS) {
    GstFormat format;
    guint64 rendered, dropped;

    gst_message_parse_qos_stats (message, &format, &rendered, &dropped);
    if (format != GST_FORMAT_UNDEFINED) {
      if (rendered != -1)
        g_atomic_int_set (&self->frames_rendered, rendered);

      if (dropped != -1)
        g_atomic_int_set (&self->frames_dropped, dropped);
    }
  }
}

static void
gst_framerate_tracer_init (GstFramerateTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  self->pid = getpid ();        // TODO: Not portable, i.e. Windows.

  self->video_sink = NULL;

  /* Init counters */
  self->frames_rendered = 0;
  self->frames_dropped = 0;
  self->last_frames_rendered = G_GUINT64_CONSTANT (0);
  self->last_frames_dropped = G_GUINT64_CONSTANT (0);
  self->max_fps = -1;
  self->min_fps = -1;
  self->fps_update_interval = GST_MSECOND * DEFAULT_FPS_UPDATE_INTERVAL_MS;

  /* init time stamps */
  self->last_ts = self->start_ts = self->interval_ts = GST_CLOCK_TIME_NONE;

  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));

  gst_tracing_register_hook (tracer, "element-post-message-pre",
      G_CALLBACK (do_element_post_message_pre));
}
