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

#ifndef __NVGSTDS_BBOXFILTER_H__
#define __NVGSTDS_BBOXFILTER_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <gst/gst.h>

typedef struct
{
  GstElement *bin;
  GstElement *sink_queue;
  GstElement *src_queue;
  GstElement *nvbboxfilter;
} NvDsBboxFilterBin;

typedef struct
{
  gboolean enable;
  gchar *aisle_calibration_file;
} NvDsBboxFilterConfig;

gboolean create_bboxfilter_bin (NvDsBboxFilterConfig * config, NvDsBboxFilterBin * bin);

#ifdef __cplusplus
}
#endif

#endif
