/*
 * Copyright (c) 2018 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#include <gst/gst.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <math.h>

#include "gstnvdsmeta.h"

#include "deepstream-360d_app.hpp"
#include "deepstream_colors.h"
#include "nvspot_result.h"

GST_DEBUG_CATEGORY_EXTERN (NVDS_APP);

GQuark _dsmeta_quark;

using namespace std;

guint instance = 0;
#define MAX_SURFACES_PER_FRAME 4

#define CEIL(a,b) ((a + b - 1) / b)

gboolean
watch_source_status (gpointer data)
{
  NvDsSrcBin *src_bin = (NvDsSrcBin *) data;

  g_print ("watch_source_status %s\n", GST_ELEMENT_NAME(src_bin));
  if (src_bin && src_bin->reconfiguring) {
    // source is still not up, reconfigure it again.
    g_timeout_add (20, reset_source_pipeline, src_bin);
    return TRUE;
  } else {
    // source is reconfigured, remove call back.
    return FALSE;
  }
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  AppCtx *appCtx = (AppCtx *) data;
  GST_CAT_DEBUG (appCtx->NVDS_APP,
      "Received message on bus: source %s, msg_type %s",
      GST_MESSAGE_SRC_NAME (message), GST_MESSAGE_TYPE_NAME (message));
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_INFO:{
      GError *error = NULL;
      gchar *debuginfo = NULL;
      gst_message_parse_info (message, &error, &debuginfo);
      g_printerr ("INFO from %s: %s\n",
          GST_OBJECT_NAME (message->src), error->message);
      if (debuginfo) {
        g_printerr ("Debug info: %s\n", debuginfo);
      }
      g_error_free (error);
      g_free (debuginfo);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *error = NULL;
      gchar *debuginfo = NULL;
      gst_message_parse_warning (message, &error, &debuginfo);
      g_printerr ("WARNING from %s: %s\n",
          GST_OBJECT_NAME (message->src), error->message);
      if (debuginfo) {
        g_printerr ("Debug info: %s\n", debuginfo);
      }
      g_error_free (error);
      g_free (debuginfo);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *error = NULL;
      gchar *debuginfo = NULL;
      guint i = 0;
      gst_message_parse_error (message, &error, &debuginfo);
      g_printerr ("ERROR from %s: %s\n",
          GST_OBJECT_NAME (message->src), error->message);
      if (debuginfo) {
        g_printerr ("Debug info: %s\n", debuginfo);
      }

      NvDsSrcParentBin *bin = &appCtx->pipeline.multi_src_bin;
      for (i = 0; i < bin->num_bins; i++) {
        if (bin->sub_bins[i].src_elem == (GstElement *) GST_MESSAGE_SRC (message))
          break;
      }

      if ((i != bin->num_bins) &&
          (appCtx->config.multi_source_config[0].type == NV_DS_SOURCE_RTSP)) {
        // Error from one of RTSP source.
        NvDsSrcBin *subBin = &bin->sub_bins[i];

        if (!subBin->reconfiguring ||
            g_strrstr(debuginfo, "500 (Internal Server Error)")) {
          if (!subBin->reconfiguring) {
            // Check status of stream every five minutes.
            g_timeout_add (60000, watch_source_status, subBin);
          }
          subBin->reconfiguring = TRUE;
          g_timeout_add (20, reset_source_pipeline, subBin);
        }
        g_error_free (error);
        g_free (debuginfo);
        return TRUE;
      }
      g_error_free (error);
      g_free (debuginfo);

      appCtx->return_value = -1;
      appCtx->quit = TRUE;
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:{
      GstState oldstate, newstate;
      gst_message_parse_state_changed (message, &oldstate, &newstate, NULL);
      if (GST_ELEMENT (GST_MESSAGE_SRC (message)) == appCtx->pipeline.pipeline) {
        switch (newstate) {
          case GST_STATE_PLAYING:
          {
            NVGSTDS_INFO_MSG_V ("Pipeline running\n");
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->pipeline.
                    pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "360d-app-playing");
          }
            break;
          case GST_STATE_PAUSED:
            if (oldstate == GST_STATE_PLAYING) {
              NVGSTDS_INFO_MSG_V ("Pipeline paused\n");
            }
            break;
          case GST_STATE_READY:
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->
                    pipeline.pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
                "360d-app-ready");
            if (oldstate == GST_STATE_NULL) {
              NVGSTDS_INFO_MSG_V ("Pipeline ready\n");
            } else {
              NVGSTDS_INFO_MSG_V ("Pipeline stopped\n");
            }
            break;
          case GST_STATE_NULL:
            g_mutex_lock (&appCtx->app_lock);
            g_cond_broadcast (&appCtx->app_cond);
            g_mutex_unlock (&appCtx->app_lock);
            break;
          default:
            break;
        }
      }
      break;
    }
    case GST_MESSAGE_EOS:
    {
      NVGSTDS_INFO_MSG_V ("Received EOS. Exiting ...\n");
      appCtx->quit = TRUE;
      return FALSE;
    }
    default:
      break;
  }

  return TRUE;
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, gpointer data)
{
  AppCtx *appCtx = (AppCtx *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ELEMENT:
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (appCtx->pipeline.multi_src_bin.bin)) {
        const GstStructure *structure;
        structure = gst_message_get_structure (msg);

       if (gst_structure_has_name (structure, "GstBinForwarded")) {
          GstMessage *child_msg;

          if (gst_structure_has_field (structure, "message")) {
            const GValue *val = gst_structure_get_value (structure, "message");
            if (G_VALUE_TYPE (val) == GST_TYPE_MESSAGE) {
              child_msg = (GstMessage *) g_value_get_boxed (val);
              if (GST_MESSAGE_TYPE (child_msg) == GST_MESSAGE_EOS) {
                guint i = 0;
                NvDsSrcParentBin *bin = &appCtx->pipeline.multi_src_bin;
                GST_DEBUG ("num bins: %d, message src: %s\n", bin->num_bins,
                         GST_MESSAGE_SRC_NAME(child_msg));
                for (i = 0; i < bin->num_bins; i++) {
                  if (bin->sub_bins[i].bin == (GstElement *) GST_MESSAGE_SRC (child_msg))
                    break;
                }

                if (i == bin->num_bins)
                  GST_ERROR ("%s: Error: No source found\n", __func__);
                else {
                  NvDsSrcBin *subBin = &bin->sub_bins[i];
                  GST_CAT_INFO (appCtx->NVDS_APP,"EOS  called %s %p %d\n,",__func__,
                      subBin, i);

                  // We have already handled the EOS for this source.
                  // It might be due to reseting of higher bin that we are
                  // getting EOS again. Ignore this.
                  if (subBin->eos_done)
                    return GST_BUS_PASS;

                  GST_CAT_INFO (appCtx->NVDS_APP,"EOS called completing %s %p %d\n,",
                      __func__, subBin, i);

                  g_mutex_lock(&subBin->bin_lock);
                  subBin->eos_done = TRUE;
                  g_mutex_unlock(&subBin->bin_lock);

                  GST_CAT_INFO (appCtx->NVDS_APP,"Reset called %s %p %d\n,",__func__,
                      subBin, i);

                  if (bin->reset_thread)
                    g_thread_unref (bin->reset_thread);
                  if (appCtx->config.multi_source_config[0].type == NV_DS_SOURCE_RTSP)
                    bin->reset_thread = g_thread_new (NULL, reset_encodebin, subBin);
                }
              } else if (GST_MESSAGE_TYPE(child_msg) == GST_MESSAGE_ASYNC_DONE) {
                guint i = 0;
                NvDsSrcParentBin *bin = &appCtx->pipeline.multi_src_bin;
                GST_DEBUG ("num bins: %d, message src: %s\n", bin->num_bins,
                           GST_MESSAGE_SRC_NAME(child_msg));
                for (i = 0; i < bin->num_bins; i++) {
                  if (bin->sub_bins[i].bin == (GstElement *) GST_MESSAGE_SRC (child_msg))
                    break;
                }

                if (i != bin->num_bins) {
                  NvDsSrcBin *subBin = &bin->sub_bins[i];
                  if (subBin->reconfiguring &&
                      appCtx->config.multi_source_config[0].type == NV_DS_SOURCE_RTSP)
                    g_timeout_add (20, set_source_to_playing, subBin);
                }
              }
            }
          }
        }
      }
      return GST_BUS_PASS;

    default:
      return GST_BUS_PASS;
  }
}

/**
 * Buffer probe function to get the results of primary infer.
 * Here it demonstrates the use by dumping bounding box coordinates in
 * kitti format.
 */
static GstPadProbeReturn
gie_processing_done_buf_prob (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
  //GstBuffer *buf = (GstBuffer *) info->data;
  NvDsInstanceBin *bin = (NvDsInstanceBin *) u_data;
  guint index = bin->index;
  AppCtx *appCtx = bin->appCtx;

  if (appCtx->instance_data[index].res_scale_factor == 0) {
    GstCaps *caps = gst_pad_get_current_caps (pad);
    if (GST_IS_CAPS (caps)) {
      const GstStructure *str = gst_caps_get_structure (caps, 0);
      gst_structure_get_int (str, "width",
          &appCtx->config.multi_source_config[index].source_width);
      gst_structure_get_int (str, "height",
          &appCtx->config.multi_source_config[index].source_height);
      gst_structure_get_fraction (str, "framerate",
          &appCtx->config.multi_source_config[index].source_fps_n,
          &appCtx->config.multi_source_config[index].source_fps_d);
      gst_caps_unref (caps);
      appCtx->instance_data[index].res_scale_factor =
          appCtx->config.multi_source_config[index].source_height / 1080.0;
    }
  }

  return GST_PAD_PROBE_OK;
}

/**
 * Probe function to get results after all inferences(Primary + Secondary)
 * are done. This will be just before OSD or sink (in case OSD is disabled).
 */
static GstPadProbeReturn
tracking_done_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
  NvDsInstanceBin *bin = (NvDsInstanceBin *) u_data;
  guint index = bin->index;
  AppCtx *appCtx = bin->appCtx;
  GstBuffer *buf = (GstBuffer *) info->data;
  GstMeta *meta;
  gpointer state = NULL;

  while ((meta = gst_buffer_iterate_meta (buf, &state))) {
    NvDsFrameMeta *params[2] = { NULL };
    if (!gst_meta_api_type_has_tag (meta->info->api, _dsmeta_quark)) {
      continue;
    }

    params[0] = (NvDsFrameMeta *) (((NvDsMeta *) meta)->meta_data);
    if (params[0]->gie_type == 1) {
      appCtx->primary_bbox_generated_cb (appCtx, buf, params, index);
    }
  }
  return GST_PAD_PROBE_OK;
}

static gboolean
seek_decode (gpointer data)
{
  NvDsSrcBin *bin = (NvDsSrcBin *) data;

  gst_element_set_state (bin->bin, GST_STATE_PAUSED);

  gst_pad_send_event (gst_element_get_static_pad (bin->tee, "sink"),
      gst_event_new_flush_start ());
  gst_pad_send_event (gst_element_get_static_pad (bin->tee, "sink"),
      gst_event_new_flush_stop (TRUE));

  gst_element_seek (bin->bin, 1.0, GST_FORMAT_TIME,
      GstSeekFlags (GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  gst_element_set_state (bin->bin, GST_STATE_PLAYING);
  return FALSE;
}

/**
 * Probe function to drop certain events to support custom
 * logic of looping of each source stream.
 */
static GstPadProbeReturn
tiler_restart_stream_buf_prob (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
  GstEvent *event = GST_EVENT (info->data);
  NvDsSrcBin *bin = (NvDsSrcBin *) u_data;
  if ((info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH)) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      g_timeout_add (1, seek_decode, bin);;
    }

    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
      GstSegment *segment;

      gst_event_parse_segment (event, (const GstSegment **)&segment);
      segment->base = bin->accumulated_base;
      bin->accumulated_base += segment->stop;
    }
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
      /* QOS events from downstream sink elements cause decoder to drop
       * frames after looping the file since the timestamps reset to 0.
       * We should drop the QOS events since we have custom logic for
       * looping individual sources. */
      case GST_EVENT_FLUSH_START:
      case GST_EVENT_FLUSH_STOP:
      case GST_EVENT_SEEK:
        return GST_PAD_PROBE_DROP;
      default:
        return GST_PAD_PROBE_OK;
    }
  }
  return GST_PAD_PROBE_OK;
}

/**
 * Function to add components to pipeline which are dependent on number
 * of streams. These components work on single buffer. If tiling is being
 * used then single instance will be created otherwise < N > such instances
 * will be created for < N > streams
 */
static gboolean
create_processing_instance (AppCtx * appCtx, guint index)
{
  gboolean ret = FALSE;
  NvDsConfig *config = &appCtx->config;
  NvDsInstanceBin *instance_bin = &appCtx->pipeline.instance_bins[index];
  GstElement *last_elem;
  gchar elem_name[32];

  instance_bin->index = index;
  instance_bin->appCtx = appCtx;
  instance = index;

  g_snprintf (elem_name, 32, "processing_bin_%d", index);
  instance_bin->bin = gst_bin_new (elem_name);

  if (!create_sink_bin (config->num_sink_sub_bins,
          config->sink_bin_sub_bin_config, &instance_bin->sink_bin, index)) {
    goto done;
  }

  gst_bin_add (GST_BIN (instance_bin->bin), instance_bin->sink_bin.bin);
  last_elem = instance_bin->sink_bin.bin;

  if (config->osd_config.enable) {
    if (!create_osd_bin (&config->osd_config, &instance_bin->osd_bin)) {
      goto done;
    }

    gst_bin_add (GST_BIN (instance_bin->bin), instance_bin->osd_bin.bin);

    NVGSTDS_LINK_ELEMENT (instance_bin->osd_bin.bin, last_elem);

    last_elem = instance_bin->osd_bin.bin;
  }

  NVGSTDS_BIN_ADD_GHOST_PAD (instance_bin->bin, last_elem, "sink");

  if (config->osd_config.enable) {
    NVGSTDS_ELEM_ADD_PROBE (instance_bin->all_bbox_buffer_probe_id,
        instance_bin->osd_bin.nvosd, "sink",
        gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
  } else {
    NVGSTDS_ELEM_ADD_PROBE (instance_bin->all_bbox_buffer_probe_id,
        instance_bin->sink_bin.bin, "sink",
        gie_processing_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, instance_bin);
  }


  ret = TRUE;
done:
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

/**
 * Function to create common elements(Primary infer, tracker, secondary infer)
 * of the pipeline. These components operate on muxed data from all the
 * streams. So they are independent of number of streams in the pipeline.
 */
gboolean
create_common_elements (NvDsConfig * config, NvDsPipeline *pipeline,
    GstElement **sink_elem, GstElement **src_elem)
{
  gboolean ret = FALSE;
  *sink_elem = *src_elem = NULL;

  pipeline->common_tee = gst_element_factory_make (NVDS_ELEM_TEE, "common_elem_tee");
  if (!pipeline->common_tee) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'common_elem_tee'");
    goto done;
  }

  pipeline->common_que = gst_element_factory_make (NVDS_ELEM_QUEUE, "common_elem_queue");
  if (!pipeline->common_que) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'common_elem_queue'");
    goto done;
  }

  gst_bin_add_many (GST_BIN (pipeline->pipeline), pipeline->common_tee,
                    pipeline->common_que, NULL);

  link_element_to_tee_src_pad (pipeline->common_tee, pipeline->common_que);

  *src_elem = pipeline->common_que;
  *sink_elem = pipeline->common_tee;

  if (config->spot_config.enable) {
    if (!create_spotanalysis_bin (&config->spot_config,
          &pipeline->common_elements.spot_bin)) {
      g_print("creating spotanalysis bin failed\n");
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline),
        pipeline->common_elements.spot_bin.bin);

    NVGSTDS_LINK_ELEMENT (pipeline->common_elements.spot_bin.bin,
                          *sink_elem);

    *sink_elem = pipeline->common_elements.spot_bin.bin;
  }

  if (config->aisle_config.enable) {
    if (!create_aisle_analysis_bin (&config->aisle_config,
                                    &pipeline->common_elements.aisle_bin)) {
      g_print ("creating aisle analysis bin failed\n");
      goto done;
    }

    gst_bin_add (GST_BIN (pipeline->pipeline),
                 pipeline->common_elements.aisle_bin.bin);

    NVGSTDS_LINK_ELEMENT (pipeline->common_elements.aisle_bin.bin,
                              *sink_elem);

    *sink_elem = pipeline->common_elements.aisle_bin.bin;
  }

  if (config->broker_config.enable) {
    pipeline->msg_broker = gst_element_factory_make (NVDS_ELEM_MSG_BROKER, "nvmsgbroker");
    if (!pipeline->msg_broker) {
      NVGSTDS_ERR_MSG_V ("Failed to create 'nvmsgbroker'");
      goto done;
    }

    gst_bin_add_many (GST_BIN (pipeline->pipeline), pipeline->msg_broker, NULL);

    g_object_set (G_OBJECT(pipeline->msg_broker), "proto-lib",
                  config->broker_config.proto_lib, "conn-str",
                  config->broker_config.conn_str, "config",
                  config->broker_config.config_file, "sync", FALSE, NULL);

    link_element_to_tee_src_pad (pipeline->common_tee, pipeline->msg_broker);
  }

  {
    if (config->tracker_config.enable) {
      if (!create_tracking_bin (&config->tracker_config,
            &pipeline->common_elements.tracker_bin)) {
        g_print("creating tracker bin failed\n");
        goto done;
      }
      gst_bin_add (GST_BIN (pipeline->pipeline),
          pipeline->common_elements.tracker_bin.bin);
      if (!*src_elem) {
        *src_elem = pipeline->common_elements.tracker_bin.bin;
      }
      if (*sink_elem) {
        NVGSTDS_LINK_ELEMENT (pipeline->common_elements.tracker_bin.bin,
            *sink_elem);
      }
      *sink_elem = pipeline->common_elements.tracker_bin.bin;
    }
  }

  if (config->enable_bboxfilter) {
    config->bboxfilter_config.aisle_calibration_file =
        g_strdup (config->aisle_config.calibration_file);

    if (!create_bboxfilter_bin (&config->bboxfilter_config,
          &pipeline->common_elements.bboxfilter_bin)) {
      g_print("creating bboxfilter bin failed\n");
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline),
        pipeline->common_elements.bboxfilter_bin.bin);
    if (!*src_elem) {
      *src_elem = pipeline->common_elements.bboxfilter_bin.bin;
    }
    if (*sink_elem) {
      NVGSTDS_LINK_ELEMENT (pipeline->common_elements.bboxfilter_bin.bin,
          *sink_elem);
    }
    *sink_elem = pipeline->common_elements.bboxfilter_bin.bin;
  }

  if (config->primary_gie_config.enable) {
    {
      if (!create_primary_gie_bin (&config->primary_gie_config,
            &pipeline->common_elements.primary_gie_bin)) {
        goto done;
      }
    }
    gst_bin_add (GST_BIN (pipeline->pipeline),
        pipeline->common_elements.primary_gie_bin.bin);
    if (!*src_elem) {
      *src_elem = pipeline->common_elements.primary_gie_bin.bin;
    }
    if (*sink_elem) {
      NVGSTDS_LINK_ELEMENT (pipeline->common_elements.primary_gie_bin.bin, *sink_elem);
    }
    *sink_elem = pipeline->common_elements.primary_gie_bin.bin;
  }


  ret = TRUE;
done:
  return ret;
}

/**
 * Main function to create the pipeline.
 */
gboolean
create_pipeline (AppCtx * appCtx,
    bbox_generated_callback primary_bbox_generated_cb,
    bbox_generated_callback all_bbox_generated_cb, perf_callback perf_cb)
{
  gboolean ret = FALSE;
  NvDsPipeline *pipeline = &appCtx->pipeline;
  NvDsConfig *config = &appCtx->config;
  GstBus *bus;
  GstElement *last_elem;
  GstElement *tmp_elem1;
  GstElement *tmp_elem2;
  guint i;
  GstPad *fps_pad;

  _dsmeta_quark = g_quark_from_static_string (NVDS_META_STRING);

  appCtx->all_bbox_generated_cb = all_bbox_generated_cb;
  appCtx->primary_bbox_generated_cb = primary_bbox_generated_cb;

  appCtx->show_bbox_text = TRUE;
  if (config->osd_config.num_out_buffers < 8) {
    config->osd_config.num_out_buffers = 8;
  }

  pipeline->pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline->pipeline) {
    NVGSTDS_ERR_MSG_V ("Failed to create pipeline");
    goto done;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline->pipeline));
  pipeline->bus_id = gst_bus_add_watch (bus, bus_callback, appCtx);
  gst_bus_set_sync_handler (bus, bus_sync_handler, appCtx, NULL);
  gst_object_unref (bus);

  /*
   * It adds muxer and < N > source components to the pipeline based
   * on the settings in configuration file.
   */
  if (!create_multi_source_bin (config->num_source_sub_bins,
      config->multi_source_config, &pipeline->multi_src_bin))
    goto done;
  gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->multi_src_bin.bin);

  if (config->streammux_config.is_parsed)
    set_streammux_properties (&config->streammux_config,
        pipeline->multi_src_bin.streammux);
  else {
    g_object_set(G_OBJECT(pipeline->multi_src_bin.streammux), "live-source",
        pipeline->multi_src_bin.live_source, NULL);
    g_object_set (G_OBJECT(pipeline->multi_src_bin.streammux), "gpu-id",
        config->multi_source_config[0].gpu_id, NULL);
    if (pipeline->multi_src_bin.sub_bins[0].depay)
      g_object_set(G_OBJECT(pipeline->multi_src_bin.streammux), "batched-push-timeout",
          1000000, NULL);
  }

  /*
   * Add probe to drop some events in case looping is required. This is done
   * to support custom logic of looping individual stream instead of whole
   * pipeline
   */
  for (i = 0; i < config->num_source_sub_bins && appCtx->config.file_loop; i++)
  {
    if (pipeline->multi_src_bin.sub_bins[i].cap_filter &&
        (pipeline->multi_src_bin.live_source == FALSE))
    {
    NVGSTDS_ELEM_ADD_PROBE (pipeline->multi_src_bin.sub_bins[i].probe_id,
        pipeline->multi_src_bin.sub_bins[i].cap_filter, "src",
        tiler_restart_stream_buf_prob,
        GstPadProbeType (GST_PAD_PROBE_TYPE_EVENT_BOTH |
            GST_PAD_PROBE_TYPE_EVENT_FLUSH),
        &pipeline->multi_src_bin.sub_bins[i]);
    }
  }

  if (config->tiled_display_config.enable) {

    /* Tiler will generate a single composited buffer for all sources. So need
     * to create only one processing instance. */
    if (!create_processing_instance (appCtx, 0)) {
        goto done;
    }

    // create and add tiling component to pipeline.
    if (config->tiled_display_config.columns * config->tiled_display_config.rows <
        config->num_source_sub_bins) {
      if (config->tiled_display_config.columns == 0) {
        config->tiled_display_config.columns =
            (guint) (sqrt (config->num_source_sub_bins) + 0.5);
      }
      config->tiled_display_config.rows =
        (guint) ceil(1.0 * config->num_source_sub_bins / config->tiled_display_config.columns);
      NVGSTDS_WARN_MSG_V("Num of Tiles less than number of sources, readjusting to "
          "%u rows, %u columns", config->tiled_display_config.rows,
          config->tiled_display_config.columns);
    }

    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->instance_bins[0].bin);
    last_elem = pipeline->instance_bins[0].bin;

    if (!create_tiled_display_bin (&config->tiled_display_config,
                                   &pipeline->tiled_display_bin)) {
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->tiled_display_bin.bin);
    NVGSTDS_LINK_ELEMENT (pipeline->tiled_display_bin.bin, last_elem);
    last_elem = pipeline->tiled_display_bin.bin;
  } else {

    /*
     * Create demuxer only if tiled display is disabled.
     */
    pipeline->demuxer =
      gst_element_factory_make (NVDS_ELEM_STREAM_DEMUX, "demuxer");
    if (!pipeline->demuxer) {
      NVGSTDS_ERR_MSG_V("Failed to create element 'demuxer'");
      goto done;
    }
    gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->demuxer);

    for (i = 0; i < config->num_source_sub_bins; i++) {
      gchar pad_name[16];
      gboolean create_instance = FALSE;
      GstPad *demux_src_pad;
      guint j;

      /* Check if any sink has been configured to render/encode output for
       * source index `i`. The processing instance for that source will be
       * created only if atleast one sink has been configured as such.
       */
      for (j = 0; j < config->num_sink_sub_bins; j++) {
        if (config->sink_bin_sub_bin_config[j].enable &&
            config->sink_bin_sub_bin_config[j].source_id == i) {
          create_instance = TRUE;
          break;
        }
      }

      if (!create_instance)
        continue;

      if (!create_processing_instance (appCtx, i)) {
        goto done;
      }
      gst_bin_add (GST_BIN (pipeline->pipeline), pipeline->instance_bins[i].bin);

      g_snprintf (pad_name, 16, "src_%02d", i);
      demux_src_pad = gst_element_get_request_pad (pipeline->demuxer, pad_name);
      NVGSTDS_LINK_ELEMENT_FULL (pipeline->demuxer, pad_name,
          pipeline->instance_bins[i].bin, "sink");
      gst_object_unref (demux_src_pad);
    }

    last_elem = pipeline->demuxer;
  }
  fps_pad = gst_element_get_static_pad (last_elem, "sink");

  if (!create_common_elements (config, pipeline, &tmp_elem1, &tmp_elem2)) {
    goto done;
  }

  if (config->primary_gie_config.enable && appCtx->primary_bbox_generated_cb &&
      !config->tracker_config.enable) {
    NVGSTDS_ELEM_ADD_PROBE (pipeline->primary_bbox_buffer_probe_id,
        pipeline->common_elements.primary_gie_bin.bin, "src",
        tracking_done_buf_prob, GST_PAD_PROBE_TYPE_BUFFER, pipeline);
  }

  if (tmp_elem2) {
    NVGSTDS_LINK_ELEMENT (tmp_elem2, last_elem);
    last_elem = tmp_elem1;
  }

  NVGSTDS_LINK_ELEMENT (pipeline->multi_src_bin.bin, last_elem);

  if (config->enable_perf_measurement) {
    appCtx->perf_struct.context = appCtx;
    if (config->multi_source_config[0].dewarper_config.enable) {
      // Max 4 surfaces are supported in case of 360d use-case
      appCtx->perf_struct.dewarper_surfaces_per_frame = MAX_SURFACES_PER_FRAME;
    }
    enable_perf_measurement (&appCtx->perf_struct, fps_pad,
        pipeline->multi_src_bin.num_bins,
        config->perf_measurement_interval_sec, perf_cb);
  }
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (appCtx->pipeline.pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "360d-app-null");


  g_mutex_init (&appCtx->app_lock);
  g_cond_init (&appCtx->app_cond);
  ret = TRUE;
done:
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

/**
 * Function to destroy pipeline and release the resources, probes etc.
 */
void
destroy_pipeline (AppCtx * appCtx)
{
  gint64 end_time;
  NvDsConfig *config = &appCtx->config;
  guint i;

  end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;

  if (!appCtx)
    return;

  if (appCtx->pipeline.multi_src_bin.reset_thread)
    g_thread_unref (appCtx->pipeline.multi_src_bin.reset_thread);

  appCtx->pipeline.multi_src_bin.reset_thread = NULL;


  if (appCtx->pipeline.demuxer) {
    gst_pad_send_event (gst_element_get_static_pad (appCtx->pipeline.demuxer,
            "sink"), gst_event_new_eos ());
  } else if (appCtx->pipeline.instance_bins[0].sink_bin.bin) {
    gst_pad_send_event (gst_element_get_static_pad (appCtx->pipeline.
            instance_bins[0].sink_bin.bin, "sink"), gst_event_new_eos ());
  }

  g_usleep (100000);

  g_mutex_lock (&appCtx->app_lock);
  if (appCtx->pipeline.pipeline) {
    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (appCtx->pipeline.pipeline));

    while (TRUE) {
      GstMessage *message = gst_bus_pop (bus);
      if (message == NULL)
        break;
      else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
        bus_callback (bus, message, appCtx);
      else
        gst_message_unref (message);
    }
    gst_element_set_state (appCtx->pipeline.pipeline, GST_STATE_NULL);
  }
  g_cond_wait_until (&appCtx->app_cond, &appCtx->app_lock, end_time);
  g_mutex_unlock (&appCtx->app_lock);

  for (i = 0; i < appCtx->config.num_source_sub_bins; i++) {
    NvDsInstanceBin *bin = &appCtx->pipeline.instance_bins[i];
    NvDsInstanceData *data = &appCtx->instance_data[i];
    if (config->osd_config.enable) {
      NVGSTDS_ELEM_REMOVE_PROBE (bin->all_bbox_buffer_probe_id,
          bin->osd_bin.nvosd, "sink");
    } else {
      NVGSTDS_ELEM_REMOVE_PROBE (bin->all_bbox_buffer_probe_id,
          bin->sink_bin.bin, "sink");
    }
    if (config->primary_gie_config.enable) {
      if (config->tracker_config.enable) {
        NVGSTDS_ELEM_REMOVE_PROBE (appCtx->pipeline.primary_bbox_buffer_probe_id,
            bin->tracker_bin.bin, "src");
      } else {
        NVGSTDS_ELEM_REMOVE_PROBE (appCtx->pipeline.primary_bbox_buffer_probe_id,
            bin->primary_gie_bin.bin, "src");
      }
    }
    if (data->bbox_params_dump_file) {
      fclose (data->bbox_params_dump_file);
    }
    if (data->bbox_params_dump_file_all) {
      fclose (data->bbox_params_dump_file_all);
    }
  }

  if (appCtx->pipeline.pipeline)
    gst_object_unref (appCtx->pipeline.pipeline);
}

gboolean
pause_pipeline (AppCtx * appCtx)
{
  GstState cur;
  GstState pending;
  GstStateChangeReturn ret;
  GstClockTime timeout = 5 * GST_SECOND / 1000;

  ret =
      gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
      timeout);

  if (ret == GST_STATE_CHANGE_ASYNC) {
    return FALSE;
  }

  if (cur == GST_STATE_PAUSED) {
    return TRUE;
  } else if (cur == GST_STATE_PLAYING) {
    gst_element_set_state (appCtx->pipeline.pipeline, GST_STATE_PAUSED);
    gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
        GST_CLOCK_TIME_NONE);
    pause_perf_measurement (&appCtx->perf_struct);
    return TRUE;
  } else {
    return FALSE;
  }
}

gboolean
resume_pipeline (AppCtx * appCtx)
{
  GstState cur;
  GstState pending;
  GstStateChangeReturn ret;
  GstClockTime timeout = 5 * GST_SECOND / 1000;

  ret =
      gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
      timeout);

  if (ret == GST_STATE_CHANGE_ASYNC) {
    return FALSE;
  }

  if (cur == GST_STATE_PLAYING) {
    return TRUE;
  } else if (cur == GST_STATE_PAUSED) {
    gst_element_set_state (appCtx->pipeline.pipeline, GST_STATE_PLAYING);
    gst_element_get_state (appCtx->pipeline.pipeline, &cur, &pending,
        GST_CLOCK_TIME_NONE);
    resume_perf_measurement (&appCtx->perf_struct);
    return TRUE;
  } else {
    return FALSE;
  }
}

void
toggle_show_bbox_text (AppCtx * appCtx)
{
  appCtx->show_bbox_text = !appCtx->show_bbox_text;
}
