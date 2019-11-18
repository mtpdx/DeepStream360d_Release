/* Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVSPOTRESULT_H_
#define _NVSPOTRESULT_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_SPOTS_PER_VIEW  16
#define MAX_AISLE_CARS_PER_VIEW  4
#define MAX_STR_LEN 32
#define MAX_CAPTURE_PTS_LEN 30
#define MAX_AISLES_PER_VIEW 4

enum NvSpotEvent {
 EVENT_PARKED = 0,
 EVENT_MOVING,
 EVENT_NONE
};

typedef struct _NvSpotRects {
  unsigned int left;
  unsigned int top;
  unsigned int width;
  unsigned int height;
}NvSpotRects;

typedef struct _NvLocation {
  float lat;
  float lon;
  float alt;
}NvLocation;

typedef struct _NvCoordinates {
  float x;
  float y;
  float z;
}NvCoordinates;

typedef struct _NvSpotCameraInfo {
  unsigned int camera_id;
  char level[MAX_STR_LEN];
  char camera_str[MAX_STR_LEN];
  char capture_pts[MAX_CAPTURE_PTS_LEN];
  NvLocation location;
  NvCoordinates coordinates;
}NvSpotCameraInfo;

typedef struct _NvSpotInfo {
  unsigned int spot_id;
  char spot_str[MAX_STR_LEN];
  char sensor_str[MAX_STR_LEN];
  NvSpotRects rect;
  unsigned int is_occupied;
  NvCoordinates coordinates;
}NvSpotInfo;

typedef struct _NvSpotResult {
  NvSpotCameraInfo camera_info;
  unsigned int num_statechanged;
  NvSpotInfo spot_view_info[MAX_SPOTS_PER_VIEW];
}NvSpotResult;

typedef struct _NvAisleRects {
  double left;
  double top;
  double width;
  double height;
}NvAisleRects;

typedef struct _NvAisleInfo {
  char aisle_grid_id [MAX_STR_LEN];
  char aisle_str [MAX_STR_LEN];
  char license_plate_str [MAX_STR_LEN];
  char license_plate_state [MAX_STR_LEN];

  unsigned long long tracker_id;
  NvAisleRects rect;
  unsigned int roi_status;

  NvLocation location;
  NvCoordinates coordinates;
}NvAisleInfo;

typedef struct _NvAisleResult {
  NvSpotCameraInfo camera_info;
  unsigned int surface_index;
  unsigned int numobjs_aisle;
  NvAisleInfo aisle_info[MAX_AISLE_CARS_PER_VIEW];
}NvAisleResult;

#ifdef __cplusplus
}
#endif

#endif
