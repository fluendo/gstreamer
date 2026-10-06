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

#ifndef __GST_MPEGH3DAAUTHPKTSBUILD_H__
#define __GST_MPEGH3DAAUTHPKTSBUILD_H__

#include "gstmpegh3dacommon.h"

G_BEGIN_DECLS

/* mpegh3daAuthStart() — ISO/IEC 23008-3 Table 233. */
typedef struct _GstMpegh3daAuthStart
{
  guint8 authID;
  guint8 authSequence;
  guint16 authHashType;
  guint16 authKeyID;
  guint32 authProvID;
  guint8 authResilienceLevelMax;
  guint8 hasAuthSequenceAUCounter;
  guint8 isFirstSequence;
  guint8 isAuthCRC;
  guint8 authFrameTypes;
  guint8 authMultiStreams;
} GstMpegh3daAuthStart;

/* mpegh3daAuthSig() — Table 234 (authPartialSig = authABREnable = 0). */
typedef struct _GstMpegh3daAuthSig
{
  guint8 authID;
  guint8 authSequence;
  guint8 authPartialSig;
  guint8 authABREnable;
  guint8 sigLengthMinus1;
  guint8 sigComplete[64];
} GstMpegh3daAuthSig;

/* mpegh3daAuthSequenceAUCounter() — Table 238. */
typedef struct _GstMpegh3daAuthSequenceAUCounter
{
  guint8 authID;
  guint32 authSequenceAUCounter;
} GstMpegh3daAuthSequenceAUCounter;

/* authTimestamp() (authTimeLong) — Table 236. */
typedef struct _GstMpegh3daAuthTimestamp
{
  guint8 authID;
  guint8 authTimeType;
  guint8 authTimeOffsetType;
  guint64 authTime;
  guint16 authTimeOffset;
} GstMpegh3daAuthTimestamp;

/* Serialize mpegh3daAuthStart() (Table 233) into a byte-aligned payload.
 * Returns the payload length in bytes, or 0 on error. */
gsize gst_mpegh3da_build_AuthStart (const GstMpegh3daAuthStart * cfg,
    guint8 * out, gsize out_size);

/* Serialize mpegh3daAuthSig() (Table 234) into a byte-aligned payload. */
gsize gst_mpegh3da_build_AuthSig (const GstMpegh3daAuthSig * cfg,
    guint8 * out, gsize out_size);

/* Serialize mpegh3daAuthSequenceAUCounter() (Table 238) into a payload. */
gsize gst_mpegh3da_build_AuthSequenceAUCounter (
    const GstMpegh3daAuthSequenceAUCounter * cfg, guint8 * out, gsize out_size);

/* Serialize authTimestamp() authTimeLong (Table 236) into a payload. */
gsize gst_mpegh3da_build_AuthTimestamp (const GstMpegh3daAuthTimestamp * cfg,
    guint8 * out, gsize out_size);

G_END_DECLS

#endif /* __GST_MPEGH3DAAUTHPKTSBUILD_H__ */
