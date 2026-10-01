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

#ifndef __GST_MPEGH3DACOMMON_H__
#define __GST_MPEGH3DACOMMON_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* Hash methods (authHashType, ISO/IEC 23008-3 Table 239). Same numeric map as
 * the video DSC plugin's hash-method enum. */
typedef enum
{
  GST_MPEGH3DA_HASH_SHA1 = 0,
  GST_MPEGH3DA_HASH_SHA224 = 1,
  GST_MPEGH3DA_HASH_SHA256 = 2,
  GST_MPEGH3DA_HASH_SHA384 = 3,
  GST_MPEGH3DA_HASH_SHA512 = 4,
} GstMpegh3daHashMethod;

#define GST_TYPE_MPEGH3DA_HASH_METHOD (gst_mpegh3da_hash_method_get_type ())
GType gst_mpegh3da_hash_method_get_type (void);

/* MHAS packet types (ISO/IEC 23008-3 Table 226). */
#define GST_MPEGH3DA_PACTYP_FILLDATA                 0
#define GST_MPEGH3DA_PACTYP_MPEGH3DACFG              1
#define GST_MPEGH3DA_PACTYP_MPEGH3DAFRAME            2
#define GST_MPEGH3DA_PACTYP_AUDIOSCENEINFO           3
#define GST_MPEGH3DA_PACTYP_SYNC                     6
#define GST_MPEGH3DA_PACTYP_SYNCGAP                  7
#define GST_MPEGH3DA_PACTYP_MARKER                   8
#define GST_MPEGH3DA_PACTYP_CRC16                    9
#define GST_MPEGH3DA_PACTYP_CRC32                    10
#define GST_MPEGH3DA_PACTYP_DESCRIPTOR               11
#define GST_MPEGH3DA_PACTYP_USERINTERACTION          12
#define GST_MPEGH3DA_PACTYP_LOUDNESS_DRC             13
#define GST_MPEGH3DA_PACTYP_BUFFERINFO               14
#define GST_MPEGH3DA_PACTYP_GLOBAL_CRC16             15
#define GST_MPEGH3DA_PACTYP_GLOBAL_CRC32             16
#define GST_MPEGH3DA_PACTYP_AUDIOTRUNCATION          17
#define GST_MPEGH3DA_PACTYP_GENDATA                  18
#define GST_MPEGH3DA_PACTYP_EARCON                   19
#define GST_MPEGH3DA_PACTYP_PCMCONFIG                20
#define GST_MPEGH3DA_PACTYP_PCMDATA                  21
#define GST_MPEGH3DA_PACTYP_LOUDNESS                 22
#define GST_MPEGH3DA_PACTYP_AUTH_START               32
#define GST_MPEGH3DA_PACTYP_AUTH_SIG                 33
#define GST_MPEGH3DA_PACTYP_UUID                     34
#define GST_MPEGH3DA_PACTYP_TIMESTAMP                35
#define GST_MPEGH3DA_PACTYP_AUTH_TAG                 36
#define GST_MPEGH3DA_PACTYP_AUTH_SEQUENCE_AU_COUNTER 37

G_END_DECLS

#endif /* __GST_MPEGH3DACOMMON_H__ */
