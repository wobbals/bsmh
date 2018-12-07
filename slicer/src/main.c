/* GStreamer
 * Copyright (C) 2000,2001,2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <string.h>
#include <math.h>
#include <stdio.h>

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst.h>

#define DB_TO_LINEAR(x) pow (10., (x) / 20.)
#define LINEAR_TO_DB(x) (20. * log10 (x))

static int file_seqno = 0;

static void attach_next_file(GstElement* pipeline) {
  char next_file_name[16];
  sprintf(next_file_name, "./%d.wav", file_seqno++);

  GstStateChangeReturn result =
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  g_assert(GST_STATE_CHANGE_SUCCESS == result);
  GstElement* new_filesink = gst_element_factory_make("filesink", NULL);
  g_object_set(G_OBJECT(new_filesink), "location", next_file_name, NULL);
}

static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  gboolean next_file_trigger = 0;

  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "level") == 0) {
      gint channels;
      GstClockTime endtime;
      gdouble rms_dB, peak_dB, decay_dB;
      gdouble rms;
      const GValue *array_val;
      const GValue *value;
      GValueArray *rms_arr, *peak_arr, *decay_arr;
      gint i;

      if (!gst_structure_get_clock_time (s, "endtime", &endtime))
        g_warning ("Could not parse endtime");

      /* the values are packed into GValueArrays with the value per channel */
      array_val = gst_structure_get_value (s, "rms");
      rms_arr = (GValueArray *) g_value_get_boxed (array_val);

      array_val = gst_structure_get_value (s, "peak");
      peak_arr = (GValueArray *) g_value_get_boxed (array_val);

      array_val = gst_structure_get_value (s, "decay");
      decay_arr = (GValueArray *) g_value_get_boxed (array_val);

      /* we can get the number of channels as the length of any of the value
       * arrays */
      channels = rms_arr->n_values;
      g_print ("endtime: %" GST_TIME_FORMAT ", channels: %d\n",
          GST_TIME_ARGS (endtime), channels);
      for (i = 0; i < channels; ++i) {

        g_print ("channel %d\n", i);
        value = g_value_array_get_nth (rms_arr, i);
        rms_dB = g_value_get_double (value);

        value = g_value_array_get_nth (peak_arr, i);
        peak_dB = g_value_get_double (value);

        value = g_value_array_get_nth (decay_arr, i);
        decay_dB = g_value_get_double (value);
        g_print ("    RMS: %f dB, peak: %f dB, decay: %f dB\n",
            rms_dB, peak_dB, decay_dB);

        /* converting from dB to normal gives us a value between 0.0 and 1.0 */
        rms = pow (10, rms_dB / 20);
        g_print ("    normalized rms value: %f\n", rms);
      }
    }
  }

  // define heuristic for swapping from one file to the next
  if (next_file_trigger) {
    // dig out the pipeline and element before final sink for relinking
    attach_next_file(NULL);
  }
  /* we handled the message we want, and ignored the ones we didn't want.
   * so the core can unref the message for us */
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *filesrc, *wavparse, *audioconvert, *level, *fakesink;
  GstElement *compressor, *volume;
  GstElement *pipeline;
  GstCaps *caps;
  GstBus *bus;
  guint watch_id;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  caps = gst_caps_from_string ("audio/x-raw,channels=2");

  pipeline = gst_pipeline_new (NULL);
  g_assert (pipeline);
  filesrc = gst_element_factory_make ("filesrc", NULL);
  g_assert (filesrc);
  wavparse = gst_element_factory_make ("wavparse", NULL);
  g_assert (wavparse);
  audioconvert = gst_element_factory_make ("audioconvert", NULL);
  g_assert (audioconvert);
  compressor = gst_element_factory_make("audiodynamic", NULL);
  g_assert(compressor);
  volume = gst_element_factory_make("volume", NULL);
  g_assert(volume);
  level = gst_element_factory_make ("level", NULL);
  g_assert (level);
  fakesink = gst_element_factory_make ("autoaudiosink", NULL);
  g_assert (fakesink);

  gst_bin_add_many(GST_BIN(pipeline), filesrc, wavparse, audioconvert,
                   compressor, volume, level,
                   fakesink, NULL);

  if (!gst_element_link (filesrc, wavparse))
    g_error ("Failed to link filesrc and wavparse");
  if (!gst_element_link (wavparse, audioconvert))
    g_error ("Failed to link wavparse and audioconvert");
  if (!gst_element_link_filtered (audioconvert, compressor, caps))
    g_error ("Failed to link audioconvert and compressor");
  if (!gst_element_link_many(compressor, volume, level, fakesink, NULL))
    g_error ("Failed to link compressor, level, and fakesink");

  g_object_set(G_OBJECT(filesrc), "location", "/Users/charley/src/bsmh/slicer/test.wav", NULL);
  /*
   use some "best guess" compressor config.
   this should probably have a smarter heuristic
   */
  g_object_set(G_OBJECT(compressor),
               /* */
               "characteristics", "soft-knee",
               /* */
               "mode", "compressor",
               /* */
               "ratio", (1.0/1.8),
               /* db_norm = pow(10, rms_db / 20) */
               "threshold", DB_TO_LINEAR(24.0),
               NULL);
  g_object_set(G_OBJECT(volume),
               /*
                target gain compensates for compressor and original track volume,
                but is fundamentally an assumption.
                again, this needs pre-analysis for tracks recorded outside
                charley's studio
                */
               "volume", DB_TO_LINEAR(12.0),
               NULL);
  g_object_set(G_OBJECT(level),
                /* make sure we'll get messages */
                "post-messages", TRUE,
                /* work on 20ms boundaries */
                "interval", 20 * GST_MSECOND,
                /* 24 dB/second falloff */
                "peak-falloff", 24.0,
                /* falloff config similar to vocal compression release */
                "peak-ttl", 50 * GST_MSECOND,
                NULL);
  /* run synced and not as fast as we can */
  g_object_set (G_OBJECT (fakesink), "sync", TRUE, NULL);

  bus = gst_element_get_bus (pipeline);
  watch_id = gst_bus_add_watch (bus, message_handler, NULL);

  GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline),
                            GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we need to run a GLib main loop to get the messages */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_source_remove (watch_id);
  g_main_loop_unref (loop);
  return 0;
}
