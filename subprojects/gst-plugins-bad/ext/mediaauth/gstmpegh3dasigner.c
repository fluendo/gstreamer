/*
 * Copyright (C) 2026 Fluendo
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

#include <string.h>

#include "gstmpegh3dasigner.h"
#include "gstmpegh3dahash.h"

#define GST_CAT_DEFAULT gst_mpegh3daauth_debug

#define DEFAULT_HASH_METHOD GST_MPEGH3DA_HASH_SHA256
#define DEFAULT_AUTH_SEQUENCE_LENGTH 30
#define DEFAULT_AUTH_ID 0
#define DEFAULT_AU_COUNTER TRUE
#define DEFAULT_TIMESTAMP FALSE

enum
{
  PROP_0,
  PROP_HASH_METHOD,
  PROP_AUTH_SEQUENCE_LENGTH,
  PROP_AUTH_ID,
  PROP_AU_COUNTER,
  PROP_TIMESTAMP,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "framed = (boolean) true, "
        "stream-format = (string) mhas, " "stream-type = (string) single"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "framed = (boolean) true, "
        "stream-format = (string) mhas, " "stream-type = (string) single"));

static void gst_mpegh3dasigner_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpegh3dasigner_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_mpegh3dasigner_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_mpegh3dasigner_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize);
static gboolean gst_mpegh3dasigner_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_mpegh3dasigner_start (GstBaseTransform * trans);
static gboolean gst_mpegh3dasigner_stop (GstBaseTransform * trans);

#define parent_class gst_mpegh3dasigner_parent_class
G_DEFINE_TYPE (GstMpegh3DASigner, gst_mpegh3dasigner, GST_TYPE_BASE_TRANSFORM);

static GstFlowReturn
gst_mpegh3dasigner_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstMapInfo inmap, outmap;
  GstBitReader br;
  GstMpegh3daMhasPacket pkt;
  gsize size;

  /* TODO(implementation): insert AUTH_START / AUTH_SEQUENCE_AU_COUNTER /
   * AUTH_TIMESTAMP / AUTH_SIG MHAS packets and hash the frame bytes. For now
   * this just parses and logs the packets, then copies the input through. */
  if (!gst_buffer_map (inbuf, &inmap, GST_MAP_READ))
    return GST_FLOW_ERROR;

  /* mpeghAudioStream(): while (bitsAvailable() != 0) mpeghAudioStreamPacket(). */
  gst_bit_reader_init (&br, inmap.data, (guint) inmap.size);
  while (gst_bit_reader_get_remaining (&br) != 0) {
    if (!gst_mpegh3da_mhas_parse_packet (&br, &pkt)) {
      GST_ERROR_OBJECT (trans, "malformed MHAS packet, stopping the parse");
      break;
    }
    gst_mpegh3da_mhas_log_packet (&pkt);
  }

  if (!gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE)) {
    gst_buffer_unmap (inbuf, &inmap);
    return GST_FLOW_ERROR;
  }

  size = MIN (inmap.size, outmap.size);
  memcpy (outmap.data, inmap.data, size);

  gst_buffer_unmap (outbuf, &outmap);
  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_set_size (outbuf, size);

  return GST_FLOW_OK;
}

static gboolean
gst_mpegh3dasigner_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps,
    gsize * othersize)
{
  /* TODO(implementation): account for the injected MHAS packets. */
  if (othersize)
    *othersize = size;
  return TRUE;
}

static gboolean
gst_mpegh3dasigner_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  /* TODO(implementation): reset sequence state on FLUSH_STOP. */
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_mpegh3dasigner_start (GstBaseTransform * trans)
{
  /* TODO(implementation): reset sequence state. */
  return TRUE;
}

static gboolean
gst_mpegh3dasigner_stop (GstBaseTransform * trans)
{
  /* TODO(implementation): reset sequence state. */
  return TRUE;
}

static void
gst_mpegh3dasigner_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (object);

  switch (prop_id) {
    case PROP_HASH_METHOD:
      self->hash_method = g_value_get_enum (value);
      break;
    case PROP_AUTH_SEQUENCE_LENGTH:
      self->auth_sequence_length = g_value_get_uint (value);
      break;
    case PROP_AUTH_ID:
      self->auth_id = g_value_get_uint (value);
      break;
    case PROP_AU_COUNTER:
      self->au_counter = g_value_get_boolean (value);
      break;
    case PROP_TIMESTAMP:
      self->timestamp = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegh3dasigner_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpegh3DASigner *self = GST_MPEGH3DASIGNER (object);

  switch (prop_id) {
    case PROP_HASH_METHOD:
      g_value_set_enum (value, self->hash_method);
      break;
    case PROP_AUTH_SEQUENCE_LENGTH:
      g_value_set_uint (value, self->auth_sequence_length);
      break;
    case PROP_AUTH_ID:
      g_value_set_uint (value, self->auth_id);
      break;
    case PROP_AU_COUNTER:
      g_value_set_boolean (value, self->au_counter);
      break;
    case PROP_TIMESTAMP:
      g_value_set_boolean (value, self->timestamp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpegh3dasigner_class_init (GstMpegh3DASignerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_mpegh3dasigner_set_property;
  gobject_class->get_property = gst_mpegh3dasigner_get_property;

  g_object_class_install_property (gobject_class, PROP_HASH_METHOD,
      g_param_spec_enum ("hash-method", "Hash method",
          "Hash algorithm to use (authHashType)",
          GST_TYPE_MPEGH3DA_HASH_METHOD, DEFAULT_HASH_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTH_SEQUENCE_LENGTH,
      g_param_spec_uint ("auth-sequence-length",
          "Authentication sequence length",
          "Number of access units per authentication sequence", 1,
          G_MAXUINT, DEFAULT_AUTH_SEQUENCE_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTH_ID,
      g_param_spec_uint ("auth-id", "Authentication ID",
          "Authentication ID (authID)", 0, 255, DEFAULT_AUTH_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AU_COUNTER,
      g_param_spec_boolean ("au-counter", "AU counter",
          "Emit PACTYP_AUTH_SEQUENCE_AU_COUNTER before each frame",
          DEFAULT_AU_COUNTER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMESTAMP,
      g_param_spec_boolean ("timestamp", "Timestamp",
          "Emit PACTYP_AUTH_TIMESTAMP (authTimeLong) once per sequence",
          DEFAULT_TIMESTAMP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "MPEG-H 3D Audio Media Authenticity Signer", "Filter/Audio",
      "Signs MPEG-H 3D audio (MHAS) streams with media authenticity",
      "Fluendo");

  transform_class->transform = GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_transform);
  transform_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_transform_size);
  transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_sink_event);
  transform_class->start = GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_start);
  transform_class->stop = GST_DEBUG_FUNCPTR (gst_mpegh3dasigner_stop);
}

static void
gst_mpegh3dasigner_init (GstMpegh3DASigner * self)
{
  self->hash_method = DEFAULT_HASH_METHOD;
  self->auth_sequence_length = DEFAULT_AUTH_SEQUENCE_LENGTH;
  self->auth_id = DEFAULT_AUTH_ID;
  self->au_counter = DEFAULT_AU_COUNTER;
  self->timestamp = DEFAULT_TIMESTAMP;

  /* In-band byte insertion grows the buffers, so this is a non-in-place
   * transform. transform() is a no-op copy until the signing is implemented. */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), FALSE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_mpegh3daauth_debug, "mpegh3daauth", 0,
      "MPEG-H 3D Audio media authenticity signer/verifier");

  return gst_element_register (plugin, "mpegh3dasigner", GST_RANK_NONE,
      GST_TYPE_MPEGH3DASIGNER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpegh3daauth,
    "MPEG-H 3D Audio media authenticity signer/verifier",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
