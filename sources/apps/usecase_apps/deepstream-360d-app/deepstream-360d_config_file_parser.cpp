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

#include "deepstream-360d_app.hpp"
#include "deepstream_common.h"
#include "deepstream_config_file_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN (APP_CFG_PARSER_CAT);

#define CONFIG_GROUP_APP "application"
#define CONFIG_GROUP_APP_DEBUG_MODE "debug-mode"
#define CONFIG_GROUP_APP_ENABLE_PERF_MEASUREMENT "enable-perf-measurement"
#define CONFIG_GROUP_APP_PERF_MEASUREMENT_INTERVAL "perf-measurement-interval-sec"
#define CONFIG_GROUP_APP_GIE_OUTPUT_DIR "gie-kitti-output-dir"

#define CONFIG_GROUP_APP_SELECT_RTP_PROTOCOL "select-rtp-protocol"
#define CONFIG_GROUP_APP_RTSP_PORT_START "rtsp-port-start"
#define CONFIG_GROUP_APP_UDP_PORT_START "udp-port-start"

#define CONFIG_GROUP_APP_ENABLE_SPOTBBOXFILTER "enable_bboxfilter"

#define CONFIG_GROUP_AISLE "aisle"
#define CONFIG_GROUP_SPOT "spot"
#define CONFIG_GROUP_BROKER "message-broker"
#define CONFIG_GROUP_SPOT_RESULT_THRESHOLD "result-threshold"

#define CONFIG_KEY_ENABLE "enable"
#define CONFIG_KEY_CALIBRATION_FILE "calibration-file"
#define CONFIG_KEY_CONFIG_FILE "proto-cfg-file"
#define CONFIG_KEY_BROKER_PROTO_LIBRARY "broker-proto-lib"
#define CONFIG_KEY_BROKER_CONNECTION_STRING "broker-conn-str"
#define CONFIG_KEY_COMPONENT_ID "component-id"
#define CONFIG_KEY_PROTO_CFG "proto-cfg"


#define CONFIG_GROUP_TESTS "tests"
#define CONFIG_GROUP_TESTS_FILE_LOOP "file-loop"

#define CHECK_ERROR(error) \
    if (error) { \
        GST_CAT_ERROR (APP_CFG_PARSER_CAT, "%s", error->message); \
        goto done; \
    }

static gboolean
parse_broker (NvDsBrokerConfig * config, GKeyFile * key_file, gchar *cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_BROKER, NULL, &error);
  CHECK_ERROR (error);

  config->config_file = NULL;

  for (key = keys; *key; key++) {
   if (!g_strcmp0 (*key, CONFIG_KEY_ENABLE)) {
      config->enable =
          g_key_file_get_boolean (key_file,
                                  CONFIG_GROUP_BROKER,
                                  CONFIG_KEY_ENABLE, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_BROKER_PROTO_LIBRARY)) {
      config->proto_lib =
          get_absolute_file_path (cfg_file_path,
                                  g_key_file_get_string (key_file,
                                                         CONFIG_GROUP_BROKER,
                                                         CONFIG_KEY_BROKER_PROTO_LIBRARY,
                                                         &error));
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_CONFIG_FILE)) {
      config->config_file =
          get_absolute_file_path (cfg_file_path,
                                  g_key_file_get_string (key_file,
                                                         CONFIG_GROUP_BROKER,
                                                         CONFIG_KEY_CONFIG_FILE,
                                                         &error));
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_BROKER_CONNECTION_STRING)) {
      config->conn_str =
          g_key_file_get_string (key_file, CONFIG_GROUP_BROKER,
                                 CONFIG_KEY_BROKER_CONNECTION_STRING, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_COMPONENT_ID)) {
      config->comp_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_BROKER,
                                  CONFIG_KEY_COMPONENT_ID, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_PROTO_CFG)) {
      // Ignore the key. This will be parsed by protocol adapter library.
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
                          CONFIG_GROUP_SPOT);
    }
  }

  if (!config->config_file) {
    config->config_file = get_absolute_file_path (cfg_file_path, NULL);
  }

  ret = TRUE;

done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

static gboolean
parse_spot (NvDsSpotConfig * config, GKeyFile * key_file, gchar *cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_SPOT, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_KEY_CALIBRATION_FILE)) {
      config->calibration_file =
          get_absolute_file_path (cfg_file_path,
                                  g_key_file_get_string (key_file,
                                        CONFIG_GROUP_SPOT,
                                        CONFIG_KEY_CALIBRATION_FILE, &error));
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_ENABLE)) {
      config->enable =
          g_key_file_get_boolean (key_file,
                                  CONFIG_GROUP_SPOT,
                                  CONFIG_KEY_ENABLE, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_SPOT_RESULT_THRESHOLD)) {
      config->result_threshold =
          g_key_file_get_integer (key_file,
                                  CONFIG_GROUP_SPOT,
                                  CONFIG_GROUP_SPOT_RESULT_THRESHOLD, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_COMPONENT_ID)) {
      config->comp_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_SPOT,
                                  CONFIG_KEY_COMPONENT_ID, &error);
      CHECK_ERROR(error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
                          CONFIG_GROUP_SPOT);
    }
  }

  ret = TRUE;

done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

static gboolean
parse_aisle (NvDsAisleConfig * config, GKeyFile * key_file, gchar *cfg_file_path)
{

  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_AISLE, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_KEY_CALIBRATION_FILE)) {
      config->calibration_file =
          get_absolute_file_path (cfg_file_path,
                                  g_key_file_get_string (key_file,
                                        CONFIG_GROUP_AISLE,
                                        CONFIG_KEY_CALIBRATION_FILE, &error));
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_ENABLE)) {
      config->enable =
          g_key_file_get_boolean (key_file,
                                  CONFIG_GROUP_AISLE,
                                  CONFIG_KEY_ENABLE, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_COMPONENT_ID)) {
      config->comp_id =
          g_key_file_get_integer (key_file, CONFIG_GROUP_AISLE,
                                  CONFIG_KEY_COMPONENT_ID, &error);
      CHECK_ERROR(error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
                          CONFIG_GROUP_AISLE);
    }
  }

  ret = TRUE;

done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

static gboolean
parse_app (NvDsConfig * config, GKeyFile * key_file, gchar *cfg_file_path)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;
  config->select_rtp_protocol = 0x7;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_APP, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_APP_ENABLE_PERF_MEASUREMENT)) {
      config->enable_perf_measurement =
          g_key_file_get_integer (key_file, CONFIG_GROUP_APP,
          CONFIG_GROUP_APP_ENABLE_PERF_MEASUREMENT, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_APP_PERF_MEASUREMENT_INTERVAL)) {
      config->perf_measurement_interval_sec =
          g_key_file_get_integer (key_file, CONFIG_GROUP_APP,
          CONFIG_GROUP_APP_PERF_MEASUREMENT_INTERVAL, &error);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_GROUP_APP_GIE_OUTPUT_DIR)) {
      config->bbox_dir_path = get_absolute_file_path (cfg_file_path,
          g_key_file_get_string (key_file, CONFIG_GROUP_APP,
          CONFIG_GROUP_APP_GIE_OUTPUT_DIR, &error));
      CHECK_ERROR (error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_APP_SELECT_RTP_PROTOCOL)) {
      config->select_rtp_protocol =
          g_key_file_get_integer(key_file,
              CONFIG_GROUP_APP,
              CONFIG_GROUP_APP_SELECT_RTP_PROTOCOL, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_APP_ENABLE_SPOTBBOXFILTER)) {
      config->enable_bboxfilter =
          g_key_file_get_integer(key_file,
              CONFIG_GROUP_APP,
              CONFIG_GROUP_APP_ENABLE_SPOTBBOXFILTER, &error);
      CHECK_ERROR(error);
    } else if (!g_strcmp0(*key, CONFIG_GROUP_APP_SELECT_RTP_PROTOCOL)) {
      config->select_rtp_protocol =
              g_key_file_get_integer(key_file,
              CONFIG_GROUP_APP,
              CONFIG_GROUP_APP_SELECT_RTP_PROTOCOL, &error);
      CHECK_ERROR(error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_APP);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

static gboolean
parse_tests (NvDsConfig * config, GKeyFile * key_file)
{
  gboolean ret = FALSE;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;

  keys = g_key_file_get_keys (key_file, CONFIG_GROUP_TESTS, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_GROUP_TESTS_FILE_LOOP)) {
      config->file_loop =
          g_key_file_get_integer (key_file, CONFIG_GROUP_TESTS,
          CONFIG_GROUP_TESTS_FILE_LOOP, &error);
      CHECK_ERROR (error);
    } else {
      NVGSTDS_WARN_MSG_V ("Unknown key '%s' for group [%s]", *key,
          CONFIG_GROUP_TESTS);
    }
  }

  ret = TRUE;
done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}

gboolean
parse_config_file (NvDsConfig * config, gchar * cfg_file_path)
{
  GKeyFile *cfg_file = g_key_file_new ();
  GError *error = NULL;
  gboolean ret = FALSE;
  gchar **groups = NULL;
  gchar **group;
  guint i, j;

  if (!APP_CFG_PARSER_CAT) {
    GST_DEBUG_CATEGORY_INIT (APP_CFG_PARSER_CAT, "NVDS_CFG_PARSER", 0, NULL);
  }

  if (!g_key_file_load_from_file (cfg_file, cfg_file_path, G_KEY_FILE_NONE,
          &error)) {
    GST_CAT_ERROR (APP_CFG_PARSER_CAT, "Failed to load uri file: %s",
        error->message);
    goto done;
  }
  groups = g_key_file_get_groups (cfg_file, NULL);

  for (group = groups; *group; group++) {
    gboolean parse_err = FALSE;
    GST_CAT_DEBUG (APP_CFG_PARSER_CAT, "Parsing group: %s", *group);
    if (!g_strcmp0 (*group, CONFIG_GROUP_APP)) {
      parse_err = !parse_app (config, cfg_file, cfg_file_path);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_SPOT)) {
      parse_err = !parse_spot (&config->spot_config, cfg_file, cfg_file_path);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_AISLE)) {
      parse_err = !parse_aisle (&config->aisle_config, cfg_file, cfg_file_path);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_BROKER)) {
      parse_err = !parse_broker (&config->broker_config, cfg_file, cfg_file_path);
    }
    if (!strncmp (*group, CONFIG_GROUP_SOURCE, sizeof (CONFIG_GROUP_SOURCE) - 1)) {
      if (config->num_source_sub_bins == MAX_SOURCE_BINS) {
        NVGSTDS_ERR_MSG_V ("App supports max %d sources", MAX_SOURCE_BINS);
        ret = FALSE;
        goto done;
      }
      parse_err = !parse_source (&config->multi_source_config[config->num_source_sub_bins],
                                 cfg_file, *group, cfg_file_path);
      if (config->multi_source_config[config->num_source_sub_bins].enable) {
        config->num_source_sub_bins++;
      }
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_STREAMMUX)) {
      parse_err = !parse_streammux (&config->streammux_config, cfg_file);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_OSD)) {
      parse_err = !parse_osd (&config->osd_config, cfg_file);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_DEWARPER)) {
      parse_err = !parse_dewarper (&config->multi_source_config[0].dewarper_config,
          cfg_file, cfg_file_path);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_PRIMARY_GIE)) {
      parse_err =
          !parse_gie (&config->primary_gie_config, cfg_file,
          (gchar*)CONFIG_GROUP_PRIMARY_GIE, cfg_file_path);
    }
    if (!g_strcmp0 (*group, CONFIG_GROUP_TRACKER)) {
      parse_err = !parse_tracker (&config->tracker_config, cfg_file);
    }
    if (!strncmp (*group, CONFIG_GROUP_SINK, sizeof (CONFIG_GROUP_SINK) - 1)) {
      if (config->num_sink_sub_bins == MAX_SINK_BINS) {
        NVGSTDS_ERR_MSG_V ("App supports max %d sinks", MAX_SINK_BINS);
        ret = FALSE;
        goto done;
      }
      parse_err =
          !parse_sink (&config->
          sink_bin_sub_bin_config[config->num_sink_sub_bins], cfg_file, *group);
      if (config->
          sink_bin_sub_bin_config[config->num_sink_sub_bins].enable) {
        config->num_sink_sub_bins++;
      }
    }

    if (!g_strcmp0 (*group, CONFIG_GROUP_TILED_DISPLAY)) {
      parse_err = !parse_tiled_display (&config->tiled_display_config, cfg_file);
    }

    if (!g_strcmp0 (*group, CONFIG_GROUP_TESTS)) {
      parse_err = !parse_tests (config, cfg_file);
    }

    if (parse_err) {
      GST_CAT_ERROR (APP_CFG_PARSER_CAT, "Failed to parse '%s' group", *group);
      goto done;
    }
  }

  for (i = 0; i < config->num_source_sub_bins; i++)
  {
    if (config->multi_source_config[0].dewarper_config.enable == 1)
    {
      // Copy 0th dewarper configuration to all sources
      config->multi_source_config[i].dewarper_config = config->multi_source_config[0].dewarper_config;
      config->multi_source_config[i].select_rtp_protocol = config->select_rtp_protocol;
    }
    if (config->multi_source_config[i].type == NV_DS_SOURCE_URI_MULTIPLE) {
      if (config->multi_source_config[i].num_sources < 1) {
        config->multi_source_config[i].num_sources = 1;
      }
      for (j = 1; j < config->multi_source_config[i].num_sources; j++) {
        if (config->num_source_sub_bins == MAX_SOURCE_BINS) {
          NVGSTDS_ERR_MSG_V ("App supports max %d sources", MAX_SOURCE_BINS);
          ret = FALSE;
          goto done;
        }
        memcpy (&config->multi_source_config[config->num_source_sub_bins],
            &config->multi_source_config[i],
            sizeof (config->multi_source_config[i]));
        config->multi_source_config[config->num_source_sub_bins].type = NV_DS_SOURCE_URI;
        config->multi_source_config[config->num_source_sub_bins].uri =
          g_strdup_printf (config->multi_source_config[config->num_source_sub_bins].uri, j);
        config->num_source_sub_bins++;
      }
      config->multi_source_config[i].type = NV_DS_SOURCE_URI;
      config->multi_source_config[i].uri =
        g_strdup_printf (config->multi_source_config[i].uri, 0);
    }
  }

  ret = TRUE;

done:
  if (cfg_file) {
    g_key_file_free (cfg_file);
  }

  if (groups) {
    g_strfreev (groups);
  }

  if (error) {
    g_error_free (error);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}
