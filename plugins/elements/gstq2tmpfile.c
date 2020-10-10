/* GStreamer
 * Copyright (C) 2020 Fluendo S.A <support@fluendo.com>
 *
 * gstq2tmpfile.c:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstq2tmpfile.h"

gboolean
gst_queue2_tmp_file_seek (GstQueue2TmpFile * file, gsize offset)
{
  g_return_val_if_fail (file, FALSE);

  return file->seek (file, offset);
}


gsize gst_queue2_tmp_file_read (GstQueue2TmpFile * file, gpointer dst,
    gsize size, gsize nmemb)
{
  g_return_val_if_fail (file, 0);

  return file->read (file, dst, size, nmemb);
}


gsize
gst_queue2_tmp_file_write (GstQueue2TmpFile * file, gconstpointer src,
    gsize size, gsize nmemb)
{
  g_return_val_if_fail (file, 0);

  return file->write (file, src, size, nmemb);
}


gint
gst_queue2_tmp_file_error (GstQueue2TmpFile * file)
{
  g_return_val_if_fail (file, -1);

  return file->error (file);
}


gint
gst_queue2_tmp_file_eof (GstQueue2TmpFile * file)
{
  g_return_val_if_fail (file, -1);

  return file->eof (file);
}


gint
gst_queue2_tmp_file_flush (GstQueue2TmpFile * file)
{
  g_return_val_if_fail (file, -1);

  return file->flush (file);
}


gint
gst_queue2_tmp_file_close (GstQueue2TmpFile * file)
{
  g_return_val_if_fail (file, -1);

  return file->close (file);
}


GstQueue2TmpFile *
gst_queue2_tmp_file_reopen (GstQueue2TmpFile * file,
    const gchar * filename, const gchar * mode)
{
  g_return_val_if_fail (file, NULL);

  return file->reopen (file, filename, mode);
}
