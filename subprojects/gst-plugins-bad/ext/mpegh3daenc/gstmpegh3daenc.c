/*
 * Copyright (C) 2026 Dolby Laboratories, Inc.
 * Copyright (C) 2026 Pablo García <pgarcia@fluendo.com>
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

#include "gstmpegh3daenc.h"

#include <gst/audio/audio.h>
#include <gst/gstclock.h>
#include <gst/gstinfo.h>

#include "impeghe_type_def.h"
#include "impeghe_drc_common.h"
#include "impeghe_drc_uni_drc.h"
#include "impeghe_drc_api.h"
#include "impeghe_dmx_cicp2geometry.h"
#include "impeghe_dmx_matrix_common.h"
#include "impeghe_memory_standards.h"
#include "impeghe_api.h"
#include "impeghe_error_standards.h"

/**
 * SECTION:element-mpegh3daenc
 * @title: mpegh3daenc
 * @short_description: MPEG-H 3D Baseline/Low Complexity Profile audio encoder
 *
 * The mpegh3daenc plugin encodes audio in MPEG-H 3D Audio Baseline/Low
 * Complexity Profile.
 */

#define SHORTNAME "MPEG-H 3D audio encoder"
#define LONGNAME                                                              \
  "MPEG-H 3D audio Baseline/Low Complexity Profile encoder"
#define DESCRIPTION                                                           \
  "Encodes audio in MPEG-H 3D Baseline/Low Complexity Profile format"
#define PLUGIN_DESCRIPTION "MPEG-H 3D audio encoder"

/* Fixed input format for this first phase (see the sink pad template). */
#define MPEGH_RATE 48000
#define MPEGH_PCM_WIDTH 16
/* Stereo default bitrate (bits/s). */
#define MPEGH_BITRATE 64000

/* Total delay ≈ 1600 samples (frame + lookahead + algorithmic). Taken from
 * libmpeghe testbench - 'start_offset_samples' */
#define LIBMPEGHE_DELAY_SAMPLES 1600

#define parent_class gst_mpegh3daenc_parent_class
G_DEFINE_TYPE_WITH_CODE (GstMpegH3DEnc, gst_mpegh3daenc,
    GST_TYPE_AUDIO_ENCODER, G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL));

GST_DEBUG_CATEGORY_STATIC (gst_mpegh3daenc_debug);
#define GST_CAT_DEFAULT gst_mpegh3daenc_debug

/* Static pad templates */
static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "channels = (int) 2, "
        "format = (string) S16LE, "
        "layout = (string) interleaved, "
        "rate = (int) " G_STRINGIFY (MPEGH_RATE)));

/* TODO: update pad template to reflect real situation (i.e, level 1 is
 * hardcoded until it's selected from a property).
 * Also, check if fields like "stream-type" are really needed apart of linking
 * with mpeghdec */
static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "stream-format = (string) mhas, "
        "framed = (boolean) true, "
        "stream-type = (string) single, "
        "profile = (string) {baseline, low-complexity}, "
        "level = (int) { 1, 2, 3, 4 }, "
        "rate = (int) " G_STRINGIFY (MPEGH_RATE)));

/* ============================================================================
 * Private encoder state
 * ============================================================================
 */

/* Holds the libmpeghe configuration and the MHAS config packet. Allocated on
 * the heap because ia_mpeghe_config_struct is larger than GObject's 64 KB
 * instance limit. */
struct _Mpegh3daencState
{
  ia_mpeghe_config_struct config;

  /* MHAS config packet (PACTYP_MPEGH3DACF from ISO/IEC 23008-3:202) */
  guint8 *config_header;
  guint config_len;
  gboolean config_sent;
};

/* Allocator passed to the library. The encoder calls it for every internal
 * buffer it needs. The +alignment over-allocation matches the library's own
 * test bench; impeghe_delete() frees these with free(), so malloc() is the
 * correct backing allocator. */
static void *
mpegh_malloc (UWORD32 size, UWORD32 alignment)
{
  return malloc (size + alignment);
}

/* TODO: remove hardcoded vals (width, bitrate, level, etc.) and double-check
 * correct initialization of all fields */
static void
gst_mpegh3daenc_init_encoder_config (Mpegh3daencState * state, guint rate,
    guint channels)
{
  ia_input_config *config = &state->config.input_config;

  /* "aud" stands for "audio" - just conventional mode, with channels and
   * stuff" */
  config->aud_ch_pcm_cfg.pcm_sz = MPEGH_PCM_WIDTH;
  config->aud_ch_pcm_cfg.sample_rate = rate;
  config->aud_ch_pcm_cfg.n_channels = channels;

  config->aud_obj_pcm_cfg.pcm_sz = MPEGH_PCM_WIDTH;
  config->aud_obj_pcm_cfg.sample_rate = rate;

  /* Higher-Order Ambisonics - scene-based audio */
  config->hoa_pcm_cfg.pcm_sz = MPEGH_PCM_WIDTH;
  config->hoa_pcm_cfg.sample_rate = rate;

  config->codec_mode = USAC_ONLY_FD;
  config->bitrate = MPEGH_BITRATE;
  config->out_fmt = 1;          /* RAW_MHAS */
  config->mhas_pkt = 1;
  config->cicp_index = 0;       /* auto-assign from channel count */

  config->enhanced_noise_filling = 1;
  config->igf_after_tns_synth = 1;
  config->tns_enable = 1;
  config->fill_elem = 1;
  config->prof_level = PROFILE_BL_LVL1;

  config->mct_mode = -1;
  config->use_vec_est = -1;

  config->oam_high_rate = 1;
}

/* ============================================================================
 * GstAudioEncoder virtual methods
 * ============================================================================
 */

static gboolean
gst_mpegh3daenc_start (GstAudioEncoder * encoder)
{
  GstMpegH3DEnc *enc = GST_MPEGH3DAENC (encoder);
  Mpegh3daencState *state;

  if (enc->state != NULL)
    return TRUE;

  state = g_new0 (Mpegh3daencState, 1);
  state->config.output_config.malloc_xaac = mpegh_malloc;

  enc->state = state;

  GST_INFO_OBJECT (enc, "Encoder state allocated.");

  return TRUE;
}

static gboolean
gst_mpegh3daenc_set_format (GstAudioEncoder * encoder, GstAudioInfo * info)
{
  GstMpegH3DEnc *enc = GST_MPEGH3DAENC (encoder);
  Mpegh3daencState *state = enc->state;
  IA_ERRORCODE err_code;
  guint rate;
  guint channels;
  guint frame_samples;
  guint frame_bytes;
  GstClockTime min_latency, max_latency;

  if ((GST_AUDIO_INFO_FORMAT (info) != GST_AUDIO_FORMAT_S16) ||
      (GST_AUDIO_INFO_RATE (info) != 48000)) {
    GST_ERROR_OBJECT (enc,
        "Unsupported input format, only S16LE, 48000 Hz for now.");
    return FALSE;
  }

  rate = GST_AUDIO_INFO_RATE (info);
  channels = GST_AUDIO_INFO_CHANNELS (info);

  /* TODO: check if channels is really needed */
  gst_mpegh3daenc_init_encoder_config (state, rate, channels);

  err_code = impeghe_create ((void *) &state->config.input_config,
      (void *) &state->config.output_config);
  if (err_code != IA_NO_ERROR) {
    GST_ERROR_OBJECT (enc, "Error creating the encoder (error code: 0x%x).",
        err_code);
    return FALSE;
  }

  err_code = impeghe_init (state->config.output_config.pv_ia_process_api_obj,
      (void *) &state->config.input_config,
      (void *) &state->config.output_config);
  if (err_code != IA_NO_ERROR) {
    GST_ERROR_OBJECT (enc, "Error initialising the encoder (error code: 0x%x).",
        err_code);
    return FALSE;
  }

  /* Init generates the MHAS config packet. We save it in the state, as it will
   * be overwritten by the first impeghe_execute() and we need it to be present
   * in the first buffer. */
  state->config_len = state->config.output_config.i_dec_len;
  state->config_header =
      g_memdup2 (state->config.output_config.
      mem_info_table[IA_MEMTYPE_OUTPUT].mem_ptr, state->config_len);
  state->config_sent = FALSE;

  /* TODO: rename these bad names */
  frame_samples = state->config.output_config.in_frame_length;
  frame_bytes = frame_samples * channels * (MPEGH_PCM_WIDTH >> 3);

  GST_INFO_OBJECT (enc,
      "Encoder initialized: %u Hz, %u channels, "
      "%u samples/frame, %u bytes/frame, %u header bytes.",
      rate, channels, frame_samples, frame_bytes, state->config_len);

  {
    /* TODO: remove hardcoded vals and read them from properties or caps */
    GstCaps *out_caps = gst_caps_new_simple ("audio/x-mpeg-h", "stream-format",
        G_TYPE_STRING, "mhas", "framed", G_TYPE_BOOLEAN, TRUE, "stream-type",
        G_TYPE_STRING, "single", "profile", G_TYPE_STRING, "baseline", "level",
        G_TYPE_INT, 1, "rate", G_TYPE_INT, rate, NULL);
    gst_audio_encoder_set_output_format (encoder, out_caps);
    gst_caps_unref (out_caps);
  }

  min_latency = (LIBMPEGHE_DELAY_SAMPLES * GST_SECOND) / rate;
  max_latency = min_latency;
  gst_audio_encoder_set_latency (encoder, min_latency, max_latency);

  gst_audio_encoder_set_frame_samples_min (encoder, frame_samples);
  gst_audio_encoder_set_frame_samples_max (encoder, frame_samples);
  gst_audio_encoder_set_frame_max (encoder, 1);

  return TRUE;
}

static GstFlowReturn
gst_mpegh3daenc_handle_frame (GstAudioEncoder * encoder, GstBuffer * buf)
{
  GstMpegH3DEnc *enc = GST_MPEGH3DAENC (encoder);
  Mpegh3daencState *state = enc->state;
  ia_output_config *output = &state->config.output_config;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf = NULL;
  GstMapInfo in_map, out_map;
  IA_ERRORCODE err_code;
  guint8 *input_buf;
  guint8 *output_buf;
  guint frame_samples;
  guint frame_bytes;
  guint out_size;
  guint prefix = 0;

  if (buf == NULL)
    return GST_FLOW_OK;

  frame_samples = output->in_frame_length;
  frame_bytes = frame_samples *
      gst_audio_encoder_get_audio_info (encoder)->channels *
      (MPEGH_PCM_WIDTH >> 3);
  input_buf = output->mem_info_table[IA_MEMTYPE_INPUT].mem_ptr;
  output_buf = output->mem_info_table[IA_MEMTYPE_OUTPUT].mem_ptr;

  if (!gst_buffer_map (buf, &in_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (enc, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  /* Copy interleaved PCM into the encoder input buffer (zero-padded). */
  /* TODO: use sth with error control for copying data? */
  memset (input_buf, 0, frame_bytes);
  memcpy (input_buf, in_map.data, MIN (in_map.size, frame_bytes));
  gst_buffer_unmap (buf, &in_map);

  err_code = impeghe_execute (output->pv_ia_process_api_obj,
      (void *) &state->config.input_config, (void *) output);
  if (err_code != IA_NO_ERROR) {
    GST_ERROR_OBJECT (enc, "Encoding error (error code: 0x%x).", err_code);
    return GST_FLOW_ERROR;
  }

  out_size = output->i_out_bytes;

  /* Prepend the MHAS config packet to the first output buffer. */
  if (!state->config_sent) {
    prefix = state->config_len;
    state->config_sent = TRUE;
  }

  out_buf =
      gst_audio_encoder_allocate_output_buffer (encoder, prefix + out_size);
  if (!out_buf) {
    GST_ERROR_OBJECT (enc, "Error allocating output buffer.");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (out_buf, &out_map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (enc, "Failed to map output buffer");
    gst_buffer_unref (out_buf);
    return GST_FLOW_ERROR;
  }

  if (prefix > 0)
    memcpy (out_map.data, state->config_header, prefix);
  memcpy (out_map.data + prefix, output_buf, out_size);

  gst_buffer_unmap (out_buf, &out_map);
  gst_buffer_resize (out_buf, 0, prefix + out_size);

  ret = gst_audio_encoder_finish_frame (encoder, out_buf, frame_samples);

  return ret;
}

/* ============================================================================
 * GObject methods
 * ============================================================================
 */

static void
gst_mpegh3daenc_finalize (GObject * object)
{
  GstMpegH3DEnc *enc = GST_MPEGH3DAENC (object);

  if (enc->state != NULL) {
    impeghe_delete ((void *) &enc->state->config.output_config);
    g_clear_pointer (&enc->state->config_header, g_free);
  }
  g_clear_pointer (&enc->state, g_free);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* ============================================================================
 * GObject type system functions (class_init, init)
 * ============================================================================
 */

static void
gst_mpegh3daenc_class_init (GstMpegH3DEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstAudioEncoderClass *base_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  base_class = (GstAudioEncoderClass *) klass;

  gobject_class->finalize = gst_mpegh3daenc_finalize;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (element_class, LONGNAME,
      "Codec/Encoder/Audio", DESCRIPTION,
      "Pablo García <pgarcia@fluendo.com>");

  base_class->start = gst_mpegh3daenc_start;
  base_class->set_format = gst_mpegh3daenc_set_format;
  base_class->handle_frame = gst_mpegh3daenc_handle_frame;
}

static void
gst_mpegh3daenc_init (GstMpegH3DEnc * enc)
{
  enc->state = NULL;

  GST_DEBUG_OBJECT (enc, "MPEG-H 3D audio encoder initialized");
}

/* ============================================================================
 * Plugin registration
 * ============================================================================
 */

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_mpegh3daenc_debug, "mpegh3daenc", 0, SHORTNAME);

  return gst_element_register (plugin, "mpegh3daenc", GST_RANK_PRIMARY,
      gst_mpegh3daenc_get_type ());
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, mpegh3daenc,
    PLUGIN_DESCRIPTION, plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
