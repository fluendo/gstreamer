/* GStreamer
 * Copyright (c) 2006 Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (c) 2009 Nokia Corporation <multimedia@maemo.org>
 * Copyright (c) 2009 Alexander Larsson <alexl@redhat.com>
 * Copyright (C) 2025 Fluendo S.A.
 *   @authors: César Fabián Orccón Chipana
 *
 * gstframerate.c: tracing module that logs framerate stats inter-process
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
 * Usage:
 * ```
 * GST_TRACERS="framerate(unix_socket_path=<address_type>:<path>)" GST_DEBUG=GST_TRACER:7 ./...
 * ```
 *
 *   unix_socket_path (optional): if set, send data through sockets
 *
 *    - address_type: "path"
 * 
 * Example:
 *
 * ```
 * GST_TRACERS="framerate(unix_socket_paths=path:/tmp/mysocket)" GST_DEBUG=GST_TRACER:7 ./...
 * ```
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/base/gstbasesink.h>
#include <gio/gunixsocketaddress.h>
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

static const gchar *CLIENT_SOCKET_DATA_FORMAT = "{"
    "\"pid\": %d, "
    "\"ts\": %" G_GUINT64_FORMAT ", "
    "\"rendered\": %" G_GUINT64_FORMAT ", "
    "\"dropped\": %" G_GUINT64_FORMAT ", "
    "\"average\": %.6f, " "\"current\": %.6f, " "\"drop rate\": %.6f" "}";

typedef struct
{
  GstFramerateTracer *self;
  gchar *data;
} GstFramerateClientSocketSendData;

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

static GSocketAddress *
socket_address_from_string (const char *name)
{
  int i, len;
  static const char *unix_socket_address_types[] = {
    "invalid",
    "anonymous",
    "path",
    "abstract",
    "padded"
  };

  for (i = 0; i < G_N_ELEMENTS (unix_socket_address_types); i++) {
    len = strlen (unix_socket_address_types[i]);
    if (!strncmp (name, unix_socket_address_types[i], len) && name[len] == ':') {
      /* Only G_UNIX_SOCKET_ADDRESS_PATH supported now. */
      if (i != G_UNIX_SOCKET_ADDRESS_PATH)
        return NULL;
      return g_unix_socket_address_new_with_type (name + len + 1, -1,
          (GUnixSocketAddressType) i);
    }
  }

  return NULL;
}

static GSocket *
gst_framerate_client_socket_open (GstFramerateTracer * self)
{
  GSocket *socket;
  GError *error = NULL;

  socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, 0, &error);
  if (socket == NULL) {
    GST_ERROR_OBJECT (self, "%s", error->message);
    g_error_free (error);
    return NULL;
  }
  GST_DEBUG_OBJECT (self, "Socket created.");

  g_socket_set_blocking (socket, FALSE);

  return socket;
}

static void
gst_framerate_client_socket_close (GstFramerateTracer * self, GSocket * socket)
{
  GError *error = NULL;

  g_return_if_fail (socket != NULL);

  if (!g_socket_close (socket, &error)) {
    GST_ERROR_OBJECT (self, "Error closing socket: %s", error->message);
  }
  g_object_unref (socket);
  if (error)
    g_error_free (error);
}

static GIOStream *
gst_framerate_client_socket_connect (GstFramerateTracer * self,
    GSocket * socket)
{
  GSocketAddress *addr;
  GSocketConnectable *connectable;
  GSocketAddressEnumerator *enumerator;
  GSocketAddress *address;
  GIOStream *connection;
  GCancellable *cancellable = NULL;
  GError *error = NULL;

  g_return_val_if_fail (socket != NULL, NULL);

  addr = socket_address_from_string (self->unix_socket_path);
  if (addr == NULL) {
    GST_ERROR_OBJECT (self, "Could not create a socket address at '%s'",
        self->unix_socket_path);
    return NULL;
  }

  connectable = G_SOCKET_CONNECTABLE (addr);
  enumerator = g_socket_connectable_enumerate (connectable);
  g_object_unref (connectable);

  while (TRUE) {
    address =
        g_socket_address_enumerator_next (enumerator, cancellable, &error);
    if (address == NULL) {
      if (error == NULL)
        GST_DEBUG_OBJECT (self, "No more addresses to try");
      else {
        GST_DEBUG_OBJECT (self, "%s", error->message);
        g_error_free (error);
      }
      return NULL;
    }

    if (g_socket_connect (socket, address, cancellable, &error)) {
      g_object_unref (address);
      break;
    }

    GST_ERROR_OBJECT (self, "Connection to %s failed: %s, trying next",
        g_unix_socket_address_get_path (G_UNIX_SOCKET_ADDRESS (address)),
        error->message);
    g_error_free (error);
    error = NULL;

    g_object_unref (address);
  }
  g_object_unref (enumerator);

  GST_DEBUG_OBJECT (self, "Connected to %s",
      g_unix_socket_address_get_path (G_UNIX_SOCKET_ADDRESS (address)));

  connection =
      G_IO_STREAM (g_socket_connection_factory_create_connection (socket));
  return connection;
}

static void
gst_framerate_client_socket_disconnect (GstFramerateTracer * self,
    GIOStream * connection)
{
  GCancellable *cancellable = NULL;
  GError *error = NULL;

  g_return_if_fail (connection != NULL);

  if (!g_io_stream_close (connection, cancellable, &error)) {
    GST_ERROR_OBJECT (self, "Error closing connection.");
    return;
  }

  g_object_unref (connection);
}

static void
gst_framerate_client_socket_send_internal (GstFramerateTracer * self,
    GIOStream * connection, gchar * data, gsize len)
{
  GOutputStream *ostream;
  GError *error = NULL;
  gsize size;

  ostream = g_io_stream_get_output_stream (connection);
  size = g_output_stream_write (ostream, data, len, NULL, &error);

  if (error != NULL) {
    GST_ERROR_OBJECT (self, "Failed to send (%ld): %s. Error: %s",
        len, data, error->message);
    g_error_free (error);
  }
  if (size != len)
    GST_ERROR_OBJECT (self, "Failed to write full data. Wrote %ld bytes", size);
}

static void
gst_framerate_client_socket_send_data_free (GstFramerateClientSocketSendData
    * user_data)
{
  g_free (user_data->data);
  g_free (user_data);
}

static gpointer
gst_framerate_client_send_on_thread (GstFramerateClientSocketSendData
    * user_data)
{
  GstFramerateTracer *self = user_data->self;
  GSocket *socket;
  GIOStream *connection;

  socket = gst_framerate_client_socket_open (self);
  if (!socket)
    return NULL;

  connection = gst_framerate_client_socket_connect (self, socket);
  if (!connection)
    return NULL;

  gst_framerate_client_socket_send_internal (self, connection, user_data->data,
      strlen (user_data->data) + 1);

  gst_framerate_client_socket_send_data_free (user_data);

  gst_framerate_client_socket_disconnect (self, connection);
  gst_framerate_client_socket_close (self, socket);

  return NULL;
}

/* hooks */

static gboolean
gst_framerate_tracer_display_current_fps (GstFramerateTracer * self,
    GstClockTime current_ts)
{
  guint64 frames_rendered, frames_dropped;
  gdouble rr, dr, average_fps;
  gdouble time_diff, time_elapsed;
  gchar data[1024];
  GstFramerateClientSocketSendData *send_data;

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
        frames_dropped, average_fps, rr, -1.0f);
    g_snprintf (data, sizeof (data), CLIENT_SOCKET_DATA_FORMAT,
        self->pid, current_ts, frames_rendered, frames_dropped, average_fps,
        rr, -1.0f);
  } else {
    gst_tracer_record_log (tr_framerate, self->pid, current_ts, frames_rendered,
        frames_dropped, -1.0f, rr, dr);
    g_snprintf (data, sizeof (data), CLIENT_SOCKET_DATA_FORMAT,
        self->pid, current_ts, frames_rendered, frames_dropped, -1.0f, rr, dr);
  }

  if (self->unix_socket_path != NULL) {
    send_data = g_new0 (GstFramerateClientSocketSendData, 1);
    send_data->self = self;
    send_data->data = g_strdup (data);
    /* This can block but as we use UNIX sockets we believe this amount is
     * infimal. Other option is to use a GThreadPool with g_thread_pool_free
     * with wait argument set to TRUE on finalize vfunc. */
    if (self->client_socket_thread)
      g_thread_join (self->client_socket_thread);

    self->client_socket_thread = g_thread_new (NULL,
        (GThreadFunc) gst_framerate_client_send_on_thread, send_data);
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

static void
gst_framerate_tracer_constructed (GObject * object)
{
  GstFramerateTracer *self = GST_FRAMERATE_TRACER (object);
  gchar *params, *tmp;
  GstStructure *params_struct = NULL;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  g_object_get (self, "params", &params, NULL);

  if (!params)
    return;

  tmp = g_strdup_printf ("framerate,%s", params);
  params_struct = gst_structure_from_string (tmp, NULL);
  g_free (tmp);

  if (params_struct) {
    const gchar *name;
    /* Set the name if assigned */
    name = gst_structure_get_string (params_struct, "name");
    if (name)
      gst_object_set_name (GST_OBJECT (self), name);

    /* Read the flags if available */
    self->unix_socket_path =
        g_strdup (gst_structure_get_string (params_struct, "unix_socket_path"));

    gst_structure_free (params_struct);
  }

  g_free (params);
}

/* tracer class */

static void
gst_framerate_tracer_finalize (GObject * object)
{
  GstFramerateTracer *self = GST_FRAMERATE_TRACER (object);

  g_free (self->unix_socket_path);
  if (self->client_socket_thread)
    g_thread_join (self->client_socket_thread);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_framerate_tracer_class_init (GstFramerateTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_framerate_tracer_constructed;
  gobject_class->finalize = gst_framerate_tracer_finalize;

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

  /* only trace pipeline framerate by default */
  self->unix_socket_path = NULL;
  self->client_socket_thread = NULL;
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
