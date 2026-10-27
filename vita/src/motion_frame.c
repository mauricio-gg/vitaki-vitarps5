// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "motion_frame.h"

void motion_frame_accel_vita_to_dualsense(const float vita_xyz[3], float out_xyz[3]) {
  out_xyz[0] = -vita_xyz[0];
  out_xyz[1] = -vita_xyz[2];
  out_xyz[2] = vita_xyz[1];
}

void motion_frame_gyro_vita_to_dualsense(const float vita_xyz[3], float out_xyz[3]) {
  out_xyz[0] = vita_xyz[0];
  out_xyz[1] = vita_xyz[2];
  out_xyz[2] = -vita_xyz[1];
}
