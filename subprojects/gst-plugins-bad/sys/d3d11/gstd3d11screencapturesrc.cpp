/*
 * GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-d3d11screencapturesrc
 * @title: d3d11screencapturesrc
 *
 * A DXGI Desktop Duplication API based screen capture element
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 d3d11screencapturesrc ! queue ! d3d11videosink
 * ```
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11screencapturesrc.h"
#include "gstd3d11screencapture.h"
#include "gstd3d11pluginutils.h"
#include "gstd3d11shader.h"
#include <wrl.h>
#include <string.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_screen_capture_debug);
#define GST_CAT_DEFAULT gst_d3d11_screen_capture_debug

enum
{
  PROP_0,
  PROP_MONITOR_INDEX,
  PROP_MONITOR_HANDLE,
  PROP_SHOW_CURSOR,

  PROP_LAST,
};

static GParamSpec *properties[PROP_LAST];

#define DEFAULT_MONITOR_INDEX -1
#define DEFAULT_SHOW_CURSOR FALSE

static GstStaticCaps template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, "BGRA") ";"
    GST_VIDEO_CAPS_MAKE ("BGRA"));

struct _GstD3D11ScreenCaptureSrc
{
  GstBaseSrc src;

  guint64 last_frame_no;
  GstClockID clock_id;
  GstVideoInfo video_info;

  GstD3D11Device *device;
  GstD3D11ScreenCapture *capture;

  GstBufferPool *pool;

  gint64 adapter_luid;
  gint monitor_index;
  HMONITOR monitor_handle;
  gboolean show_cursor;

  gboolean flushing;
  GstClockTime min_latency;
  GstClockTime max_latency;

  gboolean downstream_supports_d3d11;

  ID3D11VertexShader *vs;
  ID3D11PixelShader *ps;
  ID3D11InputLayout *layout;
  ID3D11SamplerState *sampler;
  ID3D11BlendState *blend;
};

static void gst_d3d11_screen_capture_src_dispose (GObject * object);
static void gst_d3d11_screen_capture_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_screen_capture_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_d3d11_screen_capture_src_set_context (GstElement * element,
    GstContext * context);

static GstCaps *gst_d3d11_screen_capture_src_get_caps (GstBaseSrc * bsrc,
    GstCaps * filter);
static GstCaps *gst_d3d11_screen_capture_src_fixate (GstBaseSrc * bsrc,
    GstCaps * caps);
static gboolean gst_d3d11_screen_capture_src_set_caps (GstBaseSrc * bsrc,
    GstCaps * caps);
static gboolean gst_d3d11_screen_capture_src_decide_allocation (GstBaseSrc *
    bsrc, GstQuery * query);
static gboolean gst_d3d11_screen_capture_src_start (GstBaseSrc * bsrc);
static gboolean gst_d3d11_screen_capture_src_stop (GstBaseSrc * bsrc);
static gboolean gst_d3d11_screen_capture_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_d3d11_screen_capture_src_unlock_stop (GstBaseSrc * bsrc);
static gboolean
gst_d3d11_screen_capture_src_src_query (GstBaseSrc * bsrc, GstQuery * query);

static GstFlowReturn gst_d3d11_screen_capture_src_create (GstBaseSrc * bsrc,
    guint64 offset, guint size, GstBuffer ** buf);

#define gst_d3d11_screen_capture_src_parent_class parent_class
G_DEFINE_TYPE (GstD3D11ScreenCaptureSrc, gst_d3d11_screen_capture_src,
    GST_TYPE_BASE_SRC);

static void
gst_d3d11_screen_capture_src_class_init (GstD3D11ScreenCaptureSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstCaps *caps;

  gobject_class->dispose = gst_d3d11_screen_capture_src_dispose;
  gobject_class->set_property = gst_d3d11_screen_capture_src_set_property;
  gobject_class->get_property = gst_d3d11_screen_capture_src_get_property;

  properties[PROP_MONITOR_INDEX] =
      g_param_spec_int ("monitor-index", "Monitor Index",
      "Zero-based index for monitor to capture (-1 = primary monitor)",
      -1, G_MAXINT, DEFAULT_MONITOR_INDEX,
      (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  properties[PROP_MONITOR_HANDLE] =
      g_param_spec_uint64 ("monitor-handle", "Monitor Handle",
      "A HMONITOR handle of monitor to capture",
      0, G_MAXUINT64, 0,
      (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  properties[PROP_SHOW_CURSOR] =
      g_param_spec_boolean ("show-cursor",
      "Show Mouse Cursor", "Whether to show mouse cursor",
      DEFAULT_SHOW_CURSOR,
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_set_context);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 screen capture src", "Source/Video",
      "Capture desktop image by using Desktop Duplication API",
      "Seungha Yang <seungha@centricular.com>");

  caps = gst_d3d11_get_updated_template_caps (&template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
  gst_caps_unref (caps);

  basesrc_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_get_caps);
  basesrc_class->fixate =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_fixate);
  basesrc_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_set_caps);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_decide_allocation);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_stop);
  basesrc_class->unlock =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_unlock_stop);
  basesrc_class->query =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_src_query);

  basesrc_class->create =
      GST_DEBUG_FUNCPTR (gst_d3d11_screen_capture_src_create);
}

static void
gst_d3d11_screen_capture_src_init (GstD3D11ScreenCaptureSrc * self)
{
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  self->monitor_index = DEFAULT_MONITOR_INDEX;
  self->show_cursor = DEFAULT_SHOW_CURSOR;
  self->min_latency = GST_CLOCK_TIME_NONE;
  self->max_latency = GST_CLOCK_TIME_NONE;
}

static void
gst_d3d11_screen_capture_src_dispose (GObject * object)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (object);

  gst_clear_object (&self->capture);
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_screen_capture_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (object);

  switch (prop_id) {
    case PROP_MONITOR_INDEX:
      self->monitor_index = g_value_get_int (value);
      break;
    case PROP_MONITOR_HANDLE:
      self->monitor_handle = (HMONITOR) g_value_get_uint64 (value);
      break;
    case PROP_SHOW_CURSOR:
      self->show_cursor = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static void
gst_d3d11_screen_capture_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (object);

  switch (prop_id) {
    case PROP_MONITOR_INDEX:
      g_value_set_int (value, self->monitor_index);
      break;
    case PROP_MONITOR_HANDLE:
      g_value_set_uint64 (value, (guint64) self->monitor_handle);
      break;
    case PROP_SHOW_CURSOR:
      g_value_set_boolean (value, self->show_cursor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  };
}

static void
gst_d3d11_screen_capture_src_set_context (GstElement * element,
    GstContext * context)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (element);

  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, self->adapter_luid, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstCaps *
gst_d3d11_screen_capture_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);
  GstCaps *caps = NULL;
  guint width, height;

  if (!self->capture) {
    GST_DEBUG_OBJECT (self, "Duplication object is not configured yet");
    return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  }

  if (!gst_d3d11_screen_capture_get_size (self->capture, &width, &height)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Cannot query supported resolution"), (NULL));
    return NULL;
  }

  caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  caps = gst_caps_make_writable (caps);

  gst_caps_set_simple (caps, "width", G_TYPE_INT, width, "height",
      G_TYPE_INT, height, nullptr);

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);

    caps = tmp;
  }

  return caps;
}

static GstCaps *
gst_d3d11_screen_capture_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  guint size;
  GstCaps *d3d11_caps = nullptr;

  caps = gst_caps_make_writable (caps);
  size = gst_caps_get_size (caps);

  for (guint i = 0; i < size; i++) {
    GstStructure *s;

    s = gst_caps_get_structure (caps, i);
    gst_structure_fixate_field_nearest_fraction (s, "framerate", 30, 1);

    if (!d3d11_caps) {
      GstCapsFeatures *features;
      features = gst_caps_get_features (caps, i);

      if (features && gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {

        d3d11_caps = gst_caps_new_empty ();
        gst_caps_append_structure (d3d11_caps, gst_structure_copy (s));

        gst_caps_set_features (d3d11_caps, 0,
            gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY,
                nullptr));

        break;
      }
    }
  }

  if (d3d11_caps) {
    gst_caps_unref (caps);
    caps = d3d11_caps;
  }

  return gst_caps_fixate (caps);
}

static gboolean
gst_d3d11_screen_capture_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);
  GstCapsFeatures *features;

  GST_DEBUG_OBJECT (self, "Set caps %" GST_PTR_FORMAT, caps);

  features = gst_caps_get_features (caps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    self->downstream_supports_d3d11 = TRUE;
  } else {
    self->downstream_supports_d3d11 = FALSE;
  }

  gst_video_info_from_caps (&self->video_info, caps);
  gst_base_src_set_blocksize (bsrc, GST_VIDEO_INFO_SIZE (&self->video_info));

  return TRUE;
}

static gboolean
gst_d3d11_screen_capture_src_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GstVideoInfo vinfo;

  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    gst_clear_object (&self->pool);
  }

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps) {
    GST_ERROR_OBJECT (self, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, caps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&vinfo);

    min = max = 0;
    update_pool = FALSE;
  }

  if (pool && self->downstream_supports_d3d11) {
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
      if (dpool->device != self->device)
        gst_clear_object (&pool);
    }
  }

  if (!pool) {
    if (self->downstream_supports_d3d11)
      pool = gst_d3d11_buffer_pool_new (self->device);
    else
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (self->downstream_supports_d3d11) {
    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params) {
      d3d11_params = gst_d3d11_allocation_params_new (self->device, &vinfo,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT,
          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0);
    } else {
      d3d11_params->desc[0].BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config");
    goto error;
  }

  if (self->downstream_supports_d3d11) {
    /* d3d11 buffer pool will update buffer size based on allocated texture,
     * get size from config again */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config,
        nullptr, &size, nullptr, nullptr);
    gst_structure_free (config);
  } else {
    self->pool = gst_d3d11_buffer_pool_new (self->device);

    config = gst_buffer_pool_get_config (self->pool);

    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!d3d11_params) {
      d3d11_params = gst_d3d11_allocation_params_new (self->device, &vinfo,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT, D3D11_BIND_RENDER_TARGET, 0);
    } else {
      d3d11_params->desc[0].BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
    gst_d3d11_allocation_params_free (d3d11_params);

    if (!gst_buffer_pool_set_config (self->pool, config)) {
      GST_ERROR_OBJECT (self, "Failed to set config for internal pool");
      goto error;
    }

    if (!gst_buffer_pool_set_active (self->pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Failed to activate internal pool");
      goto error;
    }
  }

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;

error:
  gst_clear_object (&self->pool);
  gst_clear_object (&pool);

  return FALSE;
}

static gboolean
gst_d3d11_screen_capture_prepare_shader (GstD3D11ScreenCaptureSrc * self)
{
  /* *INDENT-OFF* */
  static const gchar vs_str[] =
      "struct VS_INPUT {\n"
      "  float4 Position: POSITION;\n"
      "  float2 Texture: TEXCOORD;\n"
      "};\n"
      "\n"
      "struct VS_OUTPUT {\n"
      "  float4 Position: SV_POSITION;\n"
      "  float2 Texture: TEXCOORD;\n"
      "};\n"
      "\n"
      "VS_OUTPUT main (VS_INPUT input)\n"
      "{\n"
      "  return input;\n"
      "}";
  static const gchar ps_str[] =
      "Texture2D shaderTexture;\n"
      "SamplerState samplerState;\n"
      "\n"
      "struct PS_INPUT {\n"
      "  float4 Position: SV_POSITION;\n"
      "  float2 Texture: TEXCOORD;\n"
      "};\n"
      "\n"
      "struct PS_OUTPUT {\n"
      "  float4 Plane: SV_Target;\n"
      "};\n"
      "\n"
      "PS_OUTPUT main(PS_INPUT input)\n"
      "{\n"
      "  PS_OUTPUT output;\n"
      "  output.Plane = shaderTexture.Sample(samplerState, input.Texture);\n"
      "  return output;\n"
      "}";
  /* *INDENT-ON* */
  D3D11_INPUT_ELEMENT_DESC input_desc[] = {
    {"POSITION",
        0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD",
        0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
  };
  ComPtr < ID3D11VertexShader > vs;
  ComPtr < ID3D11InputLayout > layout;
  ComPtr < ID3D11PixelShader > ps;
  ComPtr < ID3D11SamplerState > sampler;
  ComPtr < ID3D11BlendState > blend;
  D3D11_SAMPLER_DESC sampler_desc;
  D3D11_BLEND_DESC blend_desc;
  ID3D11Device *device_handle;
  HRESULT hr;

  device_handle = gst_d3d11_device_get_device_handle (self->device);

  if (!gst_d3d11_create_vertex_shader (self->device,
          vs_str, input_desc, G_N_ELEMENTS (input_desc), &vs, &layout)) {
    GST_ERROR_OBJECT (self, "Failed to create vertex shader");
    return FALSE;
  }

  if (!gst_d3d11_create_pixel_shader (self->device, ps_str, &ps)) {
    GST_ERROR_OBJECT (self, "Failed to create pixel shader");
    return FALSE;
  }

  memset (&sampler_desc, 0, sizeof (D3D11_SAMPLER_DESC));
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = device_handle->CreateSamplerState (&sampler_desc, &sampler);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self,
        "Failed to create sampler state, hr 0x%x", (guint) hr);
    return FALSE;
  }

  blend_desc.AlphaToCoverageEnable = FALSE;
  blend_desc.IndependentBlendEnable = FALSE;
  blend_desc.RenderTarget[0].BlendEnable = TRUE;
  blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;

  hr = device_handle->CreateBlendState (&blend_desc, &blend);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self,
        "Failed to create blend state, hr 0x%x", (guint) hr);
    return FALSE;
  }

  self->vs = vs.Detach ();
  self->ps = ps.Detach ();
  self->layout = layout.Detach ();
  self->sampler = sampler.Detach ();
  self->blend = blend.Detach ();

  return TRUE;
}

static gboolean
gst_d3d11_screen_capture_src_start (GstBaseSrc * bsrc)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);
  GstFlowReturn ret;
  HMONITOR monitor = self->monitor_handle;
  ComPtr < IDXGIAdapter1 > adapter;
  DXGI_ADAPTER_DESC desc;
  HRESULT hr;

  if (monitor) {
    hr = gst_d3d11_screen_capture_find_output_for_monitor (monitor,
        &adapter, nullptr);
  } else if (self->monitor_index < 0) {
    hr = gst_d3d11_screen_capture_find_primary_monitor (&monitor,
        &adapter, nullptr);
  } else {
    hr = gst_d3d11_screen_capture_find_nth_monitor (self->monitor_index,
        &monitor, &adapter, nullptr);
  }

  if (FAILED (hr))
    goto error;

  hr = adapter->GetDesc (&desc);
  if (FAILED (hr))
    goto error;

  self->adapter_luid = gst_d3d11_luid_to_int64 (&desc.AdapterLuid);
  gst_clear_object (&self->device);

  if (!gst_d3d11_ensure_element_data_for_adapter_luid (GST_ELEMENT_CAST (self),
          self->adapter_luid, &self->device)) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("D3D11 device for LUID %" G_GINT64_FORMAT " is unavailble",
            self->adapter_luid), (nullptr));

    return FALSE;
  }

  self->capture = gst_d3d11_screen_capture_new (self->device, monitor);
  if (!self->capture)
    goto error;

  /* Check if we can open device */
  ret = gst_d3d11_screen_capture_prepare (self->capture);
  switch (ret) {
    case GST_D3D11_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR:
    case GST_FLOW_OK:
      break;
    case GST_D3D11_SCREEN_CAPTURE_FLOW_UNSUPPORTED:
      goto unsupported;
    default:
      goto error;
  }

  if (!gst_d3d11_screen_capture_prepare_shader (self))
    goto error;

  self->last_frame_no = -1;
  self->min_latency = self->max_latency = GST_CLOCK_TIME_NONE;

  return TRUE;

error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Failed to prepare capture object with given configuration, "
            "monitor-index: %d, monitor-handle: %p",
            self->monitor_index, self->monitor_handle), (nullptr));
    return FALSE;
  }

unsupported:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Failed to prepare capture object with given configuration, "
            "monitor-index: %d, monitor-handle: %p",
            self->monitor_index, self->monitor_handle),
        ("Try run the application on the integrated GPU"));
    return FALSE;
  }
}

static gboolean
gst_d3d11_screen_capture_src_stop (GstBaseSrc * bsrc)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);

  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    gst_clear_object (&self->pool);
  }

  GST_D3D11_CLEAR_COM (self->vs);
  GST_D3D11_CLEAR_COM (self->ps);
  GST_D3D11_CLEAR_COM (self->layout);
  GST_D3D11_CLEAR_COM (self->sampler);
  GST_D3D11_CLEAR_COM (self->blend);

  gst_clear_object (&self->capture);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_screen_capture_src_unlock (GstBaseSrc * bsrc)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);

  GST_OBJECT_LOCK (self);
  if (self->clock_id) {
    GST_DEBUG_OBJECT (self, "Waking up waiting clock");
    gst_clock_id_unschedule (self->clock_id);
  }
  self->flushing = TRUE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_d3d11_screen_capture_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);

  GST_OBJECT_LOCK (self);
  self->flushing = FALSE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_d3d11_screen_capture_src_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT_CAST (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    case GST_QUERY_LATENCY:
      if (GST_CLOCK_TIME_IS_VALID (self->min_latency)) {
        gst_query_set_latency (query,
            TRUE, self->min_latency, self->max_latency);
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
}

static GstFlowReturn
gst_d3d11_screen_capture_src_create (GstBaseSrc * bsrc, guint64 offset,
    guint size, GstBuffer ** buf)
{
  GstD3D11ScreenCaptureSrc *self = GST_D3D11_SCREEN_CAPTURE_SRC (bsrc);
  ID3D11Texture2D *texture;
  ID3D11RenderTargetView *rtv = NULL;
  gint fps_n, fps_d;
  GstMapInfo info;
  GstMemory *mem;
  GstD3D11Memory *dmem;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClock *clock = NULL;
  GstClockTime base_time;
  GstClockTime next_capture_ts;
  GstClockTime before_capture;
  GstClockTime after_capture;
  GstClockTime latency;
  GstClockTime dur;
  gboolean update_latency = FALSE;
  guint64 next_frame_no;
  gboolean draw_mouse;
  /* Just magic number... */
  gint unsupported_retry_count = 100;
  GstBuffer *buffer = NULL;
  GstBuffer *sysmem_buf = NULL;

  if (!self->capture) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
        ("Couldn't configure capture object"), (nullptr));
    return GST_FLOW_NOT_NEGOTIATED;
  }

  fps_n = GST_VIDEO_INFO_FPS_N (&self->video_info);
  fps_d = GST_VIDEO_INFO_FPS_D (&self->video_info);

  if (fps_n <= 0 || fps_d <= 0)
    return GST_FLOW_NOT_NEGOTIATED;

again:
  clock = gst_element_get_clock (GST_ELEMENT_CAST (self));
  if (!clock) {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("Cannot operate without a clock"), (nullptr));
    return GST_FLOW_ERROR;
  }

  /* Check flushing before waiting clock because we are might be doing
   * retry */
  GST_OBJECT_LOCK (self);
  if (self->flushing) {
    ret = GST_FLOW_FLUSHING;
    GST_OBJECT_UNLOCK (self);
    goto out;
  }

  base_time = GST_ELEMENT_CAST (self)->base_time;
  next_capture_ts = gst_clock_get_time (clock);
  next_capture_ts -= base_time;

  next_frame_no = gst_util_uint64_scale (next_capture_ts,
      fps_n, GST_SECOND * fps_d);

  if (next_frame_no == self->last_frame_no) {
    GstClockID id;
    GstClockReturn clock_ret;

    /* Need to wait for the next frame */
    next_frame_no += 1;

    /* Figure out what the next frame time is */
    next_capture_ts = gst_util_uint64_scale (next_frame_no,
        fps_d * GST_SECOND, fps_n);

    id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK (self),
        next_capture_ts + base_time);
    self->clock_id = id;

    /* release the object lock while waiting */
    GST_OBJECT_UNLOCK (self);

    GST_LOG_OBJECT (self, "Waiting for next frame time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (next_capture_ts));
    clock_ret = gst_clock_id_wait (id, NULL);
    GST_OBJECT_LOCK (self);

    gst_clock_id_unref (id);
    self->clock_id = NULL;

    if (clock_ret == GST_CLOCK_UNSCHEDULED) {
      /* Got woken up by the unlock function */
      ret = GST_FLOW_FLUSHING;
      GST_OBJECT_UNLOCK (self);
      goto out;
    }
    /* Duration is a complete 1/fps frame duration */
    dur = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
  } else {
    GstClockTime next_frame_ts;

    GST_LOG_OBJECT (self, "No need to wait for next frame time %"
        GST_TIME_FORMAT " next frame = %" G_GINT64_FORMAT " prev = %"
        G_GINT64_FORMAT, GST_TIME_ARGS (next_capture_ts), next_frame_no,
        self->last_frame_no);

    next_frame_ts = gst_util_uint64_scale (next_frame_no + 1,
        fps_d * GST_SECOND, fps_n);
    /* Frame duration is from now until the next expected capture time */
    dur = next_frame_ts - next_capture_ts;
  }

  self->last_frame_no = next_frame_no;
  GST_OBJECT_UNLOCK (self);

  if (!buffer) {
    if (self->downstream_supports_d3d11) {
      ret = GST_BASE_SRC_CLASS (parent_class)->alloc (bsrc,
          offset, size, &buffer);
    } else {
      if (!self->pool) {
        GST_ERROR_OBJECT (self, "Internal pool wasn't configured");
        goto error;
      }

      ret = gst_buffer_pool_acquire_buffer (self->pool, &buffer, nullptr);
    }

    if (ret != GST_FLOW_OK)
      goto out;
  }

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_ERROR_OBJECT (self, "Not a D3D11 memory");
    goto error;
  }

  dmem = (GstD3D11Memory *) mem;
  draw_mouse = self->show_cursor;
  rtv = gst_d3d11_memory_get_render_target_view (dmem, 0);
  if (draw_mouse && !rtv) {
    GST_ERROR_OBJECT (self, "Render target view is unavailable");
    goto error;
  }

  if (!gst_memory_map (mem, &info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Failed to map d3d11 memory");
    goto error;
  }

  texture = (ID3D11Texture2D *) info.data;
  before_capture = gst_clock_get_time (clock);
  ret = gst_d3d11_screen_capture_do_capture (self->capture, self->device,
      texture, rtv, self->vs, self->ps, self->layout, self->sampler,
      self->blend, draw_mouse);
  gst_memory_unmap (mem, &info);

  switch (ret) {
    case GST_D3D11_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR:
      GST_WARNING_OBJECT (self, "Got expected error, try again");
      gst_clear_object (&clock);
      goto again;
    case GST_D3D11_SCREEN_CAPTURE_FLOW_UNSUPPORTED:
      GST_WARNING_OBJECT (self, "Got DXGI_ERROR_UNSUPPORTED error");
      unsupported_retry_count--;

      if (unsupported_retry_count < 0)
        goto error;

      gst_clear_object (&clock);
      goto again;
    case GST_D3D11_SCREEN_CAPTURE_FLOW_SIZE_CHANGED:
      GST_INFO_OBJECT (self, "Size was changed, need negotiation");
      gst_clear_buffer (&buffer);
      gst_clear_object (&clock);

      if (!gst_base_src_negotiate (bsrc)) {
        GST_ERROR_OBJECT (self, "Failed to negotiate with new size");
        ret = GST_FLOW_NOT_NEGOTIATED;
        goto out;
      }
      goto again;
    default:
      break;
  }

  if (!self->downstream_supports_d3d11) {
    GstVideoFrame src_frame, dst_frame;
    gboolean copy_ret;

    ret = GST_BASE_SRC_CLASS (parent_class)->alloc (bsrc,
        offset, size, &sysmem_buf);
    if (ret != GST_FLOW_OK) {
      gst_clear_buffer (&buffer);
      goto out;
    }

    if (!gst_video_frame_map (&src_frame, &self->video_info, buffer,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Failed to map d3d11 buffer");
      goto error;
    }

    if (!gst_video_frame_map (&dst_frame, &self->video_info, sysmem_buf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "Failed to map sysmem buffer");
      gst_video_frame_unmap (&src_frame);
      goto error;
    }

    copy_ret = gst_video_frame_copy (&dst_frame, &src_frame);
    gst_video_frame_unmap (&dst_frame);
    gst_video_frame_unmap (&src_frame);

    if (!copy_ret) {
      GST_ERROR_OBJECT (self, "Failed to copy frame");
      goto error;
    }

    gst_buffer_unref (buffer);
    buffer = sysmem_buf;
    sysmem_buf = nullptr;
  }

  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_PTS (buffer) = next_capture_ts;
  GST_BUFFER_DURATION (buffer) = dur;

  after_capture = gst_clock_get_time (clock);
  latency = after_capture - before_capture;
  if (!GST_CLOCK_TIME_IS_VALID (self->min_latency)) {
    self->min_latency = self->max_latency = latency;
    update_latency = TRUE;
    GST_DEBUG_OBJECT (self, "Initial latency %" GST_TIME_FORMAT,
        GST_TIME_ARGS (latency));
  }

  if (latency > self->max_latency) {
    self->max_latency = latency;
    update_latency = TRUE;
    GST_DEBUG_OBJECT (self, "Updating max latency %" GST_TIME_FORMAT,
        GST_TIME_ARGS (latency));
  }

  if (update_latency) {
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_message_new_latency (GST_OBJECT_CAST (self)));
  }

out:
  gst_clear_object (&clock);
  *buf = buffer;

  return ret;

error:
  gst_clear_buffer (&buffer);
  gst_clear_buffer (&sysmem_buf);
  gst_clear_object (&clock);

  return GST_FLOW_ERROR;
}
