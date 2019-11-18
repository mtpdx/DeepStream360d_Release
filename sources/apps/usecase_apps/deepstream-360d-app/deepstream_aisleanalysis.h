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

#ifndef __NVGSTDS_AISLEANALYSIS_H__
#define __NVGSTDS_AISLEANALYSIS_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <gst/gst.h>

typedef struct
{
  GstElement *bin;
  GstElement *sink_queue;
  GstElement *aisle;
  GstElement *msg_conv;
} NvDsAisleBin;

typedef struct
{
  gboolean enable;
  gchar *calibration_file;
  guint comp_id;
} NvDsAisleConfig;

gboolean create_aisle_analysis_bin (NvDsAisleConfig * config, NvDsAisleBin * bin);

#ifdef __cplusplus
}
#endif
#endif
