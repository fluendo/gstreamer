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

#include <gst/audio/audio.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

/* Fixed input format of the encoder (see its sink pad template). */
#define MPEGH_RATE 48000
#define MPEGH_CHANNELS 2
#define MPEGH_PCM_WIDTH 16

/* One encoder frame: 1024 samples x 2 channels x 2 bytes (S16LE). */
#define MPEGH_FRAME_SAMPLES 1024
#define MPEGH_FRAME_BYTES                                                    \
  (MPEGH_FRAME_SAMPLES * MPEGH_CHANNELS * (MPEGH_PCM_WIDTH / 8))

#define RAW_AUDIO_CAPS_STRING                                                 \
  "audio/x-raw,format=S16LE,layout=interleaved,rate=" G_STRINGIFY (          \
      MPEGH_RATE) ",channels=" G_STRINGIFY (MPEGH_CHANNELS)

#define MPEGH_AUDIO_CAPS_STRING "audio/x-mpeg-h"

/* Push one frame of silent PCM into the harness. */
static void
push_silent_frame (GstHarness * h)
{
  GstBuffer *in_buf;

  in_buf = gst_harness_create_buffer (h, MPEGH_FRAME_BYTES);
  gst_buffer_memset (in_buf, 0, 0, MPEGH_FRAME_BYTES);

  fail_unless_equals_int (gst_harness_push (h, in_buf), GST_FLOW_OK);
}

GST_START_TEST (mpegh3daenc_element_init)
{
  GstHarness *h;

  h = gst_harness_new ("mpegh3daenc");
  fail_unless (h != NULL, "Failed to create harness");

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (mpegh3daenc_test_negotiation)
{
  GstHarness *h;
  GstCaps *out_caps;

  h = gst_harness_new ("mpegh3daenc");
  fail_unless (h != NULL, "Failed to create harness");

  gst_harness_set_src_caps_str (h, RAW_AUDIO_CAPS_STRING);
  gst_harness_set_sink_caps_str (h, MPEGH_AUDIO_CAPS_STRING);

  gst_harness_play (h);

  /* Push one frame: negotiation (and encoder setup) happens here. */
  push_silent_frame (h);

  /* Verify the output caps negotiated with downstream. */
  out_caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (out_caps != NULL, "Failed to get output caps");
  fail_unless (gst_caps_is_always_compatible (out_caps,
          gst_caps_from_string (MPEGH_AUDIO_CAPS_STRING)),
      "Output caps do not match %s", MPEGH_AUDIO_CAPS_STRING);
  gst_caps_unref (out_caps);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (mpegh3daenc_test_encode_generates_data)
{
  GstHarness *h;
  GstBuffer *out_buf;
  gboolean got_data = FALSE;
  guint n_buffers = 0;

  h = gst_harness_new ("mpegh3daenc");
  fail_unless (h != NULL, "Failed to create harness");

  gst_harness_set_src_caps_str (h, RAW_AUDIO_CAPS_STRING);
  gst_harness_set_sink_caps_str (h, MPEGH_AUDIO_CAPS_STRING);

  gst_harness_play (h);

  /* Push several frames of silence. */
  for (guint i = 0; i < 4; i++)
    push_silent_frame (h);

  /* Pull all available output and verify we produced non-empty data. */
  while ((out_buf = gst_harness_try_pull (h)) != NULL) {
    n_buffers++;
    if (gst_buffer_get_size (out_buf) > 0)
      got_data = TRUE;
    gst_buffer_unref (out_buf);
  }

  fail_unless (n_buffers > 0, "No output buffer received");
  fail_unless (got_data, "Encoder produced no data");

  gst_harness_teardown (h);
}

GST_END_TEST;

int
main (int argc, char **argv)
{
  Suite *s;
  TCase *tc_chain;

  gst_init (&argc, &argv);
  gst_check_init (&argc, &argv);

  s = suite_create ("mpegh3daenc");
  tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 15);

  tcase_add_test (tc_chain, mpegh3daenc_element_init);
  tcase_add_test (tc_chain, mpegh3daenc_test_negotiation);
  tcase_add_test (tc_chain, mpegh3daenc_test_encode_generates_data);

  return gst_check_run_suite (s, "mpegh3daenc_tests", __FILE__);
}
