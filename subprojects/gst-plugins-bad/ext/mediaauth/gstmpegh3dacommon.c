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

#include "gstmpegh3dacommon.h"

GType
gst_mpegh3da_hash_method_get_type (void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {GST_MPEGH3DA_HASH_SHA1, "SHA-1", "sha1"},
    {GST_MPEGH3DA_HASH_SHA224, "SHA-224", "sha224"},
    {GST_MPEGH3DA_HASH_SHA256, "SHA-256", "sha256"},
    {GST_MPEGH3DA_HASH_SHA384, "SHA-384", "sha384"},
    {GST_MPEGH3DA_HASH_SHA512, "SHA-512", "sha512"},
    {0, NULL, NULL}
  };

  if (G_UNLIKELY (type == 0))
    type = g_enum_register_static ("GstMpegh3dHashMethod", values);

  return type;
}
