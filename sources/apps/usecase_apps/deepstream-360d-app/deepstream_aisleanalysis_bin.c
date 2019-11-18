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

#include "deepstream_common.h"
#include "deepstream_aisleanalysis.h"

gboolean
create_aisle_analysis_bin (NvDsAisleConfig * config, NvDsAisleBin * bin)
{
  gboolean ret = FALSE;

  bin->bin = gst_bin_new ("aisle_analysis_bin");
  if (!bin->bin) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'aisle_analysis_bin'");
    goto done;
  }

  bin->sink_queue = gst_element_factory_make (NVDS_ELEM_QUEUE, "spotanalysis_sink_q");
  if (!bin->sink_queue) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'spotanalysis_sink_q'");
    goto done;
  }

  bin->aisle = gst_element_factory_make (NVDS_ELEM_NVAISLEMETADATA, "aisle_analysis");
  if (!bin->aisle) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'aisle_analysis'");
    goto done;
  }

  bin->msg_conv = gst_element_factory_make (NVDS_ELEM_MSG_CONV, "nvaislemsgconv");
  if (!bin->msg_conv) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'nvaislemsgconv'");
    goto done;
  }

  gst_bin_add_many (GST_BIN (bin->bin), bin->sink_queue, bin->aisle,
                    bin->msg_conv, NULL);

  g_object_set (G_OBJECT(bin->aisle), "aisle-calibration-file",
                config->calibration_file, "comp-id", config->comp_id, NULL);
  g_object_set (G_OBJECT(bin->msg_conv), "config",
                config->calibration_file, "comp-id", config->comp_id, NULL);

  NVGSTDS_LINK_ELEMENT (bin->sink_queue, bin->aisle);
  NVGSTDS_LINK_ELEMENT (bin->aisle, bin->msg_conv);

  NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->sink_queue, "sink");
  NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->msg_conv, "src");

  ret = TRUE;
done:

  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}
