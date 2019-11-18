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
#include "deepstream_bboxfilter.h"

gboolean
create_bboxfilter_bin (NvDsBboxFilterConfig * config, NvDsBboxFilterBin * bin)
{
  gboolean ret = FALSE;

  bin->bin = gst_bin_new ("bboxfilter_bin");
  if (!bin->bin) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'bboxfilter_bin'");
    goto done;
  }

  bin->sink_queue = gst_element_factory_make (NVDS_ELEM_QUEUE, "bboxfilter_sinkqueue");
  if (!bin->sink_queue) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'bboxfilter_sinkqueue'");
    goto done;
  }

  bin->src_queue = gst_element_factory_make (NVDS_ELEM_QUEUE, "bboxfilter_src_queue");
  if (!bin->src_queue) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'bboxfilter_src_queue'");
    goto done;
  }

  bin->nvbboxfilter = gst_element_factory_make (NVDS_ELEM_BBOXFILTER, "nvbboxfilter");
  if (!bin->nvbboxfilter) {
    NVGSTDS_ERR_MSG_V ("Failed to create 'nvbboxfilter'");
    goto done;
  }
  g_object_set (G_OBJECT (bin->nvbboxfilter), "aisle-calibration-file",
      config->aisle_calibration_file, NULL);

  gst_bin_add_many (GST_BIN (bin->bin), bin->sink_queue, bin->src_queue, bin->nvbboxfilter, NULL);


  NVGSTDS_LINK_ELEMENT (bin->sink_queue, bin->nvbboxfilter);
  NVGSTDS_LINK_ELEMENT (bin->nvbboxfilter, bin->src_queue);

  NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->sink_queue, "sink");
  NVGSTDS_BIN_ADD_GHOST_PAD (bin->bin, bin->src_queue, "src");

  ret = TRUE;
done:

  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return ret;
}
