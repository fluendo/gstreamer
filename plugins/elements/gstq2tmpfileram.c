/* GStreamer
 * Copyright (C) 2020 Fluendo S.A <support@fluendo.com>
 *
 * gstq2tmpfileram.c:
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

#include <glib/gstdio.h>

/* Class to work with malloc'ed memchunk, that pretends it's a file. */

typedef struct _GstQueue2TmpFileRAM GstQueue2TmpFileRAM;
struct _GstQueue2TmpFileRAM
{
  GstQueue2TmpFile parent;

  GPtrArray *memchunks;
  gsize chunk_size;

  gsize cursor;
  gsize end;

  /* What "ferror" will return.
   * Currently no real stdio's code is needed, just "-1"
   * is enough for any type of error. */
  gint error;

  GMutex lock;
};
#define RF_LOCK(x) g_mutex_lock (&x->lock)
#define RF_UNLOCK(x) g_mutex_unlock (&x->lock)


/* fseek */
static gboolean
gst_queue2_tmp_file_ram_seek (GstQueue2TmpFile * file, gsize offset)
{
  GstQueue2TmpFileRAM *rf = (GstQueue2TmpFileRAM *) file;
  gboolean ret = FALSE;

  RF_LOCK (rf);

  /* Seek position must not point outside of the file */
  if (rf->end >= offset) {
    rf->cursor = offset;
    ret = TRUE;
  } else {
    rf->error = -1;
  }

  RF_UNLOCK (rf);

  return ret;
}


/* fread */
static gsize
gst_queue2_tmp_file_ram_read (GstQueue2TmpFile * file, gpointer dst,
    gsize size, gsize nmemb)
{
  GstQueue2TmpFileRAM *rf = (GstQueue2TmpFileRAM *) file;
  gsize bytes_to_read;
  gsize items_to_read;
  gchar *cdst = (gchar *) dst;

  if (G_UNLIKELY (size == 0))
    return 0;

  RF_LOCK (rf);

  /* We also must handle the case when there's not enough data in the file,
   * so we can't read all items requested. For fread() this case is valid,
   * we should just read the items we can. */
  items_to_read = MIN (nmemb, (rf->end - rf->cursor) / size);

  /* Following fread()'s behaviour, number of bytes to read is anigned to number
   * of items available in the file.
   *
   * bytes_to_read is going to decrease in the loop,
   * items_to_read is the decided return value, it won't change */
  bytes_to_read = items_to_read * size;

  /* Iterate by memchunks we use to store the data, until we read all the bytes.
   * This loop should never fail. */
  while (bytes_to_read) {
    gint chunk_num;
    gchar *chunk;
    guint offset_in_chunk;
    guint to_read_in_chunk;

    chunk_num = rf->cursor / rf->chunk_size;
    /* chunk_num can never be outside of the array boundaries, that's
     * controlled by rf->cursor & rf->end */
    chunk = (gchar *) g_ptr_array_index (rf->memchunks, chunk_num);

    offset_in_chunk = rf->cursor - (chunk_num * rf->chunk_size);
    to_read_in_chunk = MIN (bytes_to_read, rf->chunk_size - offset_in_chunk);

    memcpy (cdst, chunk + offset_in_chunk, to_read_in_chunk);

    dst += to_read_in_chunk;
    bytes_to_read -= to_read_in_chunk;
    rf->cursor += to_read_in_chunk;
  }

  RF_UNLOCK (rf);
  return items_to_read;
}


/* fwrite */
static gsize
gst_queue2_tmp_file_ram_write (GstQueue2TmpFile * file, gconstpointer src,
    gsize size, gsize nmemb)
{
  GstQueue2TmpFileRAM *rf = (GstQueue2TmpFileRAM *) file;
  gsize bytes_to_write = nmemb * size;
  gchar *csrc = (gchar *) src;

  RF_LOCK (rf);

  while (bytes_to_write) {
    gint chunk_num;
    gchar *chunk;
    guint offset_in_chunk;
    guint to_write_in_chunk;

    /* Fill chunk until the end, then pick or create next one */
    chunk_num = rf->cursor / rf->chunk_size;

    /* Grow */
    if (chunk_num >= rf->memchunks->len) {
      g_ptr_array_add (rf->memchunks, g_malloc (rf->chunk_size));
    }

    chunk = (gchar *) g_ptr_array_index (rf->memchunks, chunk_num);

    offset_in_chunk = rf->cursor - (chunk_num * rf->chunk_size);
    to_write_in_chunk = MIN (bytes_to_write, rf->chunk_size - offset_in_chunk);

    memcpy (chunk + offset_in_chunk, csrc, to_write_in_chunk);

    src += to_write_in_chunk;
    bytes_to_write -= to_write_in_chunk;
    rf->cursor += to_write_in_chunk;
  }

  rf->end = MAX (rf->end, rf->cursor);
  RF_UNLOCK (rf);
  /* This function never fails */
  return nmemb;
}


/* ferror */
static gint
gst_queue2_tmp_file_ram_error (GstQueue2TmpFile * file)
{
  GstQueue2TmpFileRAM *rf = (GstQueue2TmpFileRAM *) file;

  return rf->error;
}


/* feof */
static gint
gst_queue2_tmp_file_ram_eof (GstQueue2TmpFile * file)
{
  GstQueue2TmpFileRAM *rf = (GstQueue2TmpFileRAM *) file;
  gint ret;

  RF_LOCK (rf);
  ret = rf->end == rf->cursor ? 1 : 0;
  RF_UNLOCK (rf);

  return ret;
}


/* fflush */
static gint
gst_queue2_tmp_file_ram_flush (GstQueue2TmpFile * file)
{
  /* No-op for ram file */
  return 0;
}


/* fclose , destructor */
static gint
gst_queue2_tmp_file_ram_close (GstQueue2TmpFile * file)
{
  GstQueue2TmpFileRAM *rf = (GstQueue2TmpFileRAM *) file;

  g_ptr_array_free (rf->memchunks, TRUE);
  g_mutex_clear (&rf->lock);
  g_free (rf);
  return 0;
}


/* freopen. */
static GstQueue2TmpFile *
gst_queue2_tmp_file_ram_reopen (GstQueue2TmpFile * file,
    const gchar * filename, const gchar * mode)
{
  /* No-op for ram file */
  return file;
}


/* fopen */
GstQueue2TmpFile *
gst_queue2_tmp_file_ram_open (gsize grow_step)
{
  GstQueue2TmpFileRAM *ret;

  if (G_UNLIKELY (grow_step == 0))
    return NULL;

  ret = g_new0 (GstQueue2TmpFileRAM, 1);

  ret->chunk_size = grow_step;
  ret->memchunks = g_ptr_array_new_full (0, g_free);
  g_mutex_init (&ret->lock);

  ret->parent.seek = gst_queue2_tmp_file_ram_seek;
  ret->parent.read = gst_queue2_tmp_file_ram_read;
  ret->parent.write = gst_queue2_tmp_file_ram_write;
  ret->parent.error = gst_queue2_tmp_file_ram_error;
  ret->parent.eof = gst_queue2_tmp_file_ram_eof;
  ret->parent.flush = gst_queue2_tmp_file_ram_flush;
  ret->parent.close = gst_queue2_tmp_file_ram_close;
  ret->parent.reopen = gst_queue2_tmp_file_ram_reopen;

  return (GstQueue2TmpFile *) ret;
}
