// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/**
 * @file motion_frame.h
 * @brief Axis conversion from the Vita's motion frame to the DualSense frame (GH #455)
 *
 * sceMotionGetState() reports in the Vita's own frame: X right, Y toward the top of the screen,
 * Z out of the screen. The PS5 reads the DualSense frame (the SDL/Chiaki convention): X right,
 * Y out of the face (up when the pad lies flat), Z toward the player. The change of frame is the
 * proper rotation (x, y, z) -> (x, z, -y). Sent as is, a flat Vita made the console think the
 * controller stood on its edge.
 *
 * Accelerometer: the Vita reports the gravity vector (flat face up reads about (0, 0, -1)) while
 * the DualSense reports the reaction to gravity (flat reads (0, +1, 0)), so the accel is also
 * negated. The gyro is a rotation rate and only takes the frame change.
 *
 * No SDK types, so it is checked natively (test/motion_frame_tests.c).
 */

#pragma once

/**
 * Converts a Vita accelerometer sample (G, gravity vector) to the DualSense frame (G, reaction to
 * gravity). Result: (-x, -z, +y).
 *
 * @param vita_xyz Vita accel, 3 floats.
 * @param out_xyz  DualSense accel, 3 floats; must not alias vita_xyz.
 */
void motion_frame_accel_vita_to_dualsense(const float vita_xyz[3], float out_xyz[3]);

/**
 * Converts a Vita gyro sample (rad/s, right-handed about the Vita axes) to the DualSense frame
 * (rad/s, right-handed about the DualSense axes). Result: (x, z, -y).
 *
 * @param vita_xyz Vita gyro, 3 floats.
 * @param out_xyz  DualSense gyro, 3 floats; must not alias vita_xyz.
 */
void motion_frame_gyro_vita_to_dualsense(const float vita_xyz[3], float out_xyz[3]);
