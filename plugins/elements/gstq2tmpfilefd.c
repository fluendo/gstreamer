/* GStreamer
 * Copyright (C) 2020 Fluendo S.A <support@fluendo.com>
 *
 * gstq2tmpfilefd.c:
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

#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#else
#include <unistd.h>
#endif

/* Class to work with temporary file in FS. Code is moved from gstqueue2's
 * implementation with no change */

typedef struct _GstQueue2TmpFileFd GstQueue2TmpFileFd;
struct _GstQueue2TmpFileFd
{
  GstQueue2TmpFile parent;
  FILE *fd;
};


static inline FILE *
gst_queue2_tmp_file_fd (GstQueue2TmpFile * file)
{
  /* file is guaranteed to be non-NULL by parent class */
  return ((GstQueue2TmpFileFd *) file)->fd;
}


/* fseek */
static gboolean
gst_queue2_tmp_file_fd_seek (GstQueue2TmpFile * file, gsize offset)
{
  FILE *fd = gst_queue2_tmp_file_fd (file);

#ifdef HAVE_FSEEKO
  return fseeko (fd, (off_t) offset, SEEK_SET) != 0;
#elif defined (G_OS_UNIX) || defined (G_OS_WIN32)
  return lseek (fileno (fd), (off_t) offset, SEEK_SET) == (off_t) - 1;
#else
  return fseek (fd, offset, SEEK_SET) != 0;
#endif
}


/* fread */
static gsize gst_queue2_tmp_file_fd_read (GstQueue2TmpFile * file, gpointer dst,
    gsize size, gsize nmemb)
{
  FILE *fd = gst_queue2_tmp_file_fd (file);

  return fread (dst, size, nmemb, fd);
}


/* fwrite */
static gsize
gst_queue2_tmp_file_fd_write (GstQueue2TmpFile * file, gconstpointer src,
    gsize size, gsize nmemb)
{
  FILE *fd = gst_queue2_tmp_file_fd (file);

  return fwrite (src, size, nmemb, fd);
}


/* ferror */
static gint
gst_queue2_tmp_file_fd_error (GstQueue2TmpFile * file)
{
  FILE *fd = gst_queue2_tmp_file_fd (file);

  return ferror (fd);
}


/* feof */
static gint
gst_queue2_tmp_file_fd_eof (GstQueue2TmpFile * file)
{
  FILE *fd = gst_queue2_tmp_file_fd (file);

  return feof (fd);
}


/* fflush */
static gint
gst_queue2_tmp_file_fd_flush (GstQueue2TmpFile * file)
{
  FILE *fd = gst_queue2_tmp_file_fd (file);

  return fflush (fd);
}


/* fclose , destructor */
static gint
gst_queue2_tmp_file_fd_close (GstQueue2TmpFile * file)
{
  gint ret = 0;
  FILE *fd = gst_queue2_tmp_file_fd (file);

  if (G_LIKELY (fd))
    ret = fclose (fd);

  g_free (file);
  return ret;
}


/* freopen. */
static GstQueue2TmpFile *
gst_queue2_tmp_file_fd_reopen (GstQueue2TmpFile * file,
    const gchar * filename, const gchar * mode)
{
  GstQueue2TmpFileFd *self = (GstQueue2TmpFileFd *) file;

  self->fd = freopen (filename, mode, self->fd);

  if (G_UNLIKELY (!self->fd)) {
    /* fd is already closed by libc at this point.
     * Just use the destructor to free the structure. */
    gst_queue2_tmp_file_fd_close (file);
    return NULL;
  }

  return file;
}


/* fopen / g_mkstemp + fdopen */
GstQueue2TmpFile *
gst_queue2_tmp_file_fd_open (gchar * name,
    gboolean is_template, GstQueue2TmpFileFdOpenError * err)
{
  GstQueue2TmpFileFd *ret;
  FILE *fd;
  gint ffd = -1;

  if (is_template) {
    ffd = g_mkstemp (name);
    if (G_UNLIKELY (ffd == -1)) {
      if (err)
        *err = GST_QUEUE2_TMP_FILE_FD_OPEN_ERROR_CREATE;
      return NULL;
    }

    fd = fdopen (ffd, "wb+");
  } else {
    fd = g_fopen (name, "wb+");
  }

  if (G_UNLIKELY (!fd)) {
    if (ffd != -1)
      close (ffd);
    if (err)
      *err = GST_QUEUE2_TMP_FILE_FD_OPEN_ERROR_OPEN;
    return NULL;
  }

  ret = g_new (GstQueue2TmpFileFd, 1);

  ret->fd = fd;
  ret->parent.seek = gst_queue2_tmp_file_fd_seek;
  ret->parent.read = gst_queue2_tmp_file_fd_read;
  ret->parent.write = gst_queue2_tmp_file_fd_write;
  ret->parent.error = gst_queue2_tmp_file_fd_error;
  ret->parent.eof = gst_queue2_tmp_file_fd_eof;
  ret->parent.flush = gst_queue2_tmp_file_fd_flush;
  ret->parent.close = gst_queue2_tmp_file_fd_close;
  ret->parent.reopen = gst_queue2_tmp_file_fd_reopen;

  return (GstQueue2TmpFile *)ret;
}
