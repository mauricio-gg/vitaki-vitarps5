// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Vita -> DualSense motion axis conversion (GH #455). A wrong
// sign or axis here makes the console see the controller on its edge or tilting the wrong way.
// `./tools/build.sh test` only cross-compiles for arm-vita-eabi and never executes, so run these
// natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include \
//      test/motion_frame_tests.c vita/src/motion_frame.c -o /tmp/motion_frame_tests && \
//      /tmp/motion_frame_tests

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "motion_frame.h"

#define EPS 1e-6f

static void expect_vec(const char *what, const float got[3], float x, float y, float z) {
  if (fabsf(got[0] - x) > EPS || fabsf(got[1] - y) > EPS || fabsf(got[2] - z) > EPS) {
    fprintf(stderr, "FAIL %s: got (%.4f, %.4f, %.4f), want (%.4f, %.4f, %.4f)\n", what, got[0],
            got[1], got[2], x, y, z);
    exit(1);
  }
}

// Catches: a flat Vita read as standing on its edge (Astro's Playroom controller falls to the
// bottom). The logged flat Vita sample must come out as the DualSense flat reading (0, +1, 0).
static void test_flat_vita_reads_flat_dualsense(void) {
  const float vita[3] = {-0.01f, 0.04f, -0.98f}, expect[3] = {0.01f, 0.98f, 0.04f};
  float out[3];
  motion_frame_accel_vita_to_dualsense(vita, out);
  expect_vec("flat", out, expect[0], expect[1], expect[2]);
}

// Catches: an upright-facing-player Vita mapped to the wrong end of the Z axis. Gravity pulls
// down the Vita's -Y; the DualSense held upright with its face toward the player has Z pointing
// down, so the reaction (up) is -Z.
static void test_upright_facing_player_reads_minus_z(void) {
  const float vita[3] = {0.0f, -1.0f, 0.0f};
  float out[3];
  motion_frame_accel_vita_to_dualsense(vita, out);
  expect_vec("upright", out, 0.0f, 0.0f, -1.0f);
}

// Catches: gyro and accel using different frames, which makes the tracker's orientation spin the
// wrong way: a turn about the Vita's Y (up through the screen top) must be a turn about the
// DualSense's -Z, and a turn about the Vita's Z (out of the screen) a turn about the DualSense's Y.
static void test_gyro_follows_the_same_rotation_as_accel(void) {
  const float about_vita_y[3] = {0.0f, 1.0f, 0.0f}, about_vita_z[3] = {0.0f, 0.0f, 1.0f};
  float out[3];
  motion_frame_gyro_vita_to_dualsense(about_vita_y, out);
  expect_vec("gyro y", out, 0.0f, 0.0f, -1.0f);
  motion_frame_gyro_vita_to_dualsense(about_vita_z, out);
  expect_vec("gyro z", out, 0.0f, 1.0f, 0.0f);
}

int main(void) {
  test_flat_vita_reads_flat_dualsense();
  test_upright_facing_player_reads_minus_z();
  test_gyro_follows_the_same_rotation_as_accel();
  puts("motion_frame_tests: all passed");
  return 0;
}
