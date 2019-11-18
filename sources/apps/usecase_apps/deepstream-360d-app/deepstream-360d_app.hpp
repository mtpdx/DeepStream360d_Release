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

#ifndef __NVGSTDS_360D_APP_H__
#define __NVGSTDS_360D_APP_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <gst/gst.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include "deepstream_common.h"
#include "deepstream_config.h"
#include "deepstream_osd.h"
#include "deepstream_streammux.h"
#include "deepstream_dewarper.h"
#include "deepstream_metadata_pool.h"
#include "deepstream_perf.h"
#include "deepstream_primary_gie.h"
#include "deepstream_sinks.h"
#include "deepstream_sources.h"
#include "deepstream_tiled_display.h"
#include "deepstream_tracker.h"
#include "deepstream_spotanalysis.h"
#include "deepstream_aisleanalysis.h"
#include "deepstream_bboxfilter.h"
#include "deepstream_app_version.h"

#define MAX_CATEGORY_LEN 32

typedef struct _AppCtx AppCtx;

typedef struct
{
  gboolean enable;
  gchar *proto_lib;
  gchar *conn_str;
  gchar *config_file;
  guint comp_id;
} NvDsBrokerConfig;

typedef struct
{
  guint index;
  GstElement *bin;
  NvDsPrimaryGieBin primary_gie_bin;
  NvDsOSDBin osd_bin;
  NvDsDewarperBin dewarper_bin;
  NvDsTrackerBin tracker_bin;
  NvDsSpotBin spot_bin;
  NvDsAisleBin aisle_bin;
  NvDsBboxFilterBin bboxfilter_bin;
  NvDsSinkBin sink_bin;
  gulong all_bbox_buffer_probe_id;
  gulong fps_buffer_probe_id;
  AppCtx *appCtx;
} NvDsInstanceBin;

typedef struct
{
  GstElement *pipeline;
  GstElement *msg_broker;
  GstElement *common_tee;
  GstElement *common_que;
  NvDsSrcParentBin multi_src_bin;
  NvDsInstanceBin instance_bins[MAX_SOURCE_BINS];
  NvDsInstanceBin common_elements;
  NvDsTiledDisplayBin tiled_display_bin;
  GstElement *demuxer;
  gulong primary_bbox_buffer_probe_id;
  gulong spotanalysis_buffer_probe_id;
  guint bus_id;
} NvDsPipeline;

typedef struct
{
  gboolean display_roi_marking;
  gboolean all_bbox_file_dump;
  gboolean enable_perf_measurement;
  gboolean flow_original_res;
  guint perf_measurement_interval_sec;
  gboolean enable_padding;
  gint batching_method;
  gint pipeline_width;
  gint pipeline_height;
  gint enable_bboxfilter;
  gint stop_rec_latency;
  gint roi_entry_latency;
  gint roi_exit_latency;
  guint num_source_sub_bins;
  NvDsStreammuxConfig streammux_config;
  NvDsSourceConfig multi_source_config[MAX_SOURCE_BINS];
  NvDsOSDConfig osd_config;
  NvDsGieConfig primary_gie_config;
  NvDsTrackerConfig tracker_config;
  NvDsSpotConfig spot_config;
  NvDsAisleConfig aisle_config;
  NvDsBrokerConfig broker_config;
  NvDsBboxFilterConfig bboxfilter_config;
  guint num_sink_sub_bins;
  NvDsSinkSubBinConfig sink_bin_sub_bin_config[MAX_SINK_BINS];
  NvDsTiledDisplayConfig tiled_display_config;
  gint file_loop;
  gchar *bbox_dir_path;
  gint select_rtp_protocol;
  gboolean debug_mode;
} NvDsConfig;

typedef struct
{
  NvDsFrameMeta *params_list[100];
  guint bbox_list_size;

  gulong frame_num;

  FILE *bbox_params_dump_file;
  FILE *bbox_params_dump_file_all;

  gdouble res_scale_factor;
} NvDsInstanceData;

gboolean parse_config_file (NvDsConfig * config, gchar * cfg_file_path);
void save_config_to_file (AppCtx *appCtx, NvDsConfig * config, gchar * save_file_path);
typedef void (*bbox_generated_callback) (AppCtx *appCtx, GstBuffer * buf, NvDsFrameMeta ** params, guint index);

struct _AppCtx
{
  gboolean version;
  gchar *cfgfile;
  gchar *input_file_path;
  NvDsPipeline pipeline;
  NvDsConfig config;
  NvDsInstanceData instance_data[MAX_SOURCE_BINS];
  gint return_value;
  NvDsAppPerfStructInt perf_struct;
  bbox_generated_callback primary_bbox_generated_cb;
  bbox_generated_callback all_bbox_generated_cb;
  NvDsMetaPool meta_pool;
  gboolean show_app_graphics;
  gboolean show_bbox_text;
  gboolean seeking;
  gboolean quit;
  GMutex app_lock;
  GCond app_cond;
  guint index;
  gchar hostname[HOST_NAME_MAX + 1];
  gchar category_name[MAX_CATEGORY_LEN];
  GST_DEBUG_CATEGORY (NVDS_APP);
};

gboolean create_pipeline (AppCtx * appCtx,
    bbox_generated_callback primary_bbox_generated_cb,
    bbox_generated_callback all_bbox_generated_cb,
    perf_callback perf_cb);

gboolean pause_pipeline (AppCtx * appCtx);
gboolean resume_pipeline (AppCtx * appCtx);
gboolean seek_pipeline (AppCtx * appCtx, glong milliseconds, gboolean seek_is_relative);

void toggle_show_bbox_text (AppCtx * appCtx);
void goto_next_frame (NvDsInstanceData * appCtx);
void goto_previous_frame (NvDsInstanceData * appCtx);

void destroy_pipeline (AppCtx * appCtx);
void restart_pipeline (AppCtx * appCtx);

#ifdef __cplusplus
}
#endif

#endif
