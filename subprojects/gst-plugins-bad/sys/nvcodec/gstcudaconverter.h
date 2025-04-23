/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#pragma once

#include <gst/video/video.h>
#include <gst/cuda/gstcuda.h>

G_BEGIN_DECLS

#define GST_TYPE_CUDA_CONVERTER             (gst_cuda_converter_get_type())
#define GST_CUDA_CONVERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_CONVERTER,GstCudaConverter))
#define GST_CUDA_CONVERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CUDA_CONVERTER,GstCudaConverterClass))
#define GST_CUDA_CONVERTER_GET_CLASS(obj)   (GST_CUDA_CONVERTER_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_CUDA_CONVERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_CONVERTER))
#define GST_IS_CUDA_CONVERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CUDA_CONVERTER))
#define GST_CUDA_CONVERTER_CAST(obj)        ((GstCudaConverter*)(obj))

typedef struct _GstCudaConverter GstCudaConverter;
typedef struct _GstCudaConverterClass GstCudaConverterClass;
typedef struct _GstCudaConverterPrivate GstCudaConverterPrivate;

struct _GstCudaConverter
{
  GstObject parent;

  GstCudaContext *context;

  /*< private >*/
  GstCudaConverterPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstCudaConverterClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_cuda_converter_get_type (void);

GstCudaConverter *  gst_cuda_converter_new (const GstVideoInfo * in_info,
                                            const GstVideoInfo * out_info,
                                            GstCudaContext * context,
                                            GstStructure * config);

gboolean            gst_cuda_converter_convert_frame (GstCudaConverter * converter,
                                                      GstVideoFrame * src_frame,
                                                      GstVideoFrame * dst_frame,
                                                      CUstream cuda_stream,
                                                      gboolean * synchronized);

G_END_DECLS

