/* GStreamer
 * Copyright (C) 2020 Fluendo S.A <support@fluendo.com>
 *
 * gstq2tmpfile.h:
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


#ifndef __GST_QUEUE2_TMP_FILE_H__
#define __GST_QUEUE2_TMP_FILE_H__

#include <glib.h>

G_BEGIN_DECLS
/* Abstraction that can be eather temporary file in FS, eather just some
 * allocated RAM in user space, depending on the constructor used to open it:
 * gst_queue2_tmp_file_fd_open - temporary file in FS 
 * gst_queue2_tmp_file_ram_open - allocated RAM in user space */
typedef struct _GstQueue2TmpFile GstQueue2TmpFile;

/* Enum to distinguish gst_queue2_tmp_file_fd_open failures */
typedef enum _GstQueue2TmpFileFdOpenError GstQueue2TmpFileFdOpenError;
enum _GstQueue2TmpFileFdOpenError
{
  GST_QUEUE2_TMP_FILE_FD_OPEN_ERROR_CREATE,
  GST_QUEUE2_TMP_FILE_FD_OPEN_ERROR_OPEN
};

/* fopen / g_mkstemp + fdopen.
 * Will change data under "name" if is_template == TRUE */
GstQueue2TmpFile * gst_queue2_tmp_file_fd_open (gchar *name,
    gboolean is_template, GstQueue2TmpFileFdOpenError * err);

/* use FILE's interface, but with RAM */
GstQueue2TmpFile *gst_queue2_tmp_file_ram_open (gsize pre_alloc,
    gsize grow_step);

/* fseek */
gboolean gst_queue2_tmp_file_seek (GstQueue2TmpFile * file, gsize offset);

/* fread */
gsize gst_queue2_tmp_file_read (GstQueue2TmpFile * file, gpointer dst,
    gsize size, gsize nmemb);

/* fwrite */
gsize gst_queue2_tmp_file_write (GstQueue2TmpFile * file, gconstpointer src,
    gsize size, gsize nmemb);

/* ferror */
gint gst_queue2_tmp_file_error (GstQueue2TmpFile * file);

/* feof */
gint gst_queue2_tmp_file_eof (GstQueue2TmpFile * file);

/* fflush */
gint gst_queue2_tmp_file_flush (GstQueue2TmpFile * file);

/* fclose */
gint gst_queue2_tmp_file_close (GstQueue2TmpFile * file);

/* freopen. no-op for "ram file". */
GstQueue2TmpFile *gst_queue2_tmp_file_reopen (GstQueue2TmpFile * file,
    const gchar * filename, const gchar * mode);


/* Private. Parent class for implementations */
struct _GstQueue2TmpFile
{
  gboolean (*seek) (GstQueue2TmpFile * file, gsize offset);

  gsize (*read) (GstQueue2TmpFile * file, gpointer dst,
      gsize size, gsize nmemb);

  gsize (*write) (GstQueue2TmpFile * file, gconstpointer src,
      gsize size, gsize nmemb);

  gint (*error) (GstQueue2TmpFile * file);

  gint (*eof) (GstQueue2TmpFile * file);

  gint (*flush) (GstQueue2TmpFile * file);

  gint (*close) (GstQueue2TmpFile * file);

  GstQueue2TmpFile *(*reopen) (GstQueue2TmpFile * file,
      const gchar * filename, const gchar * mode);
};


G_END_DECLS
#endif /* __GST_QUEUE2_TMP_FILE_H__ */
