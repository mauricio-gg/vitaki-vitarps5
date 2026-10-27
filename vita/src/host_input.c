#include "context.h"
#include "debug_tools.h"
#include "front_touch_zone.h"
#include "host_input.h"
#include "motion_frame.h"
#include "touchpad_gesture.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <chiaki/orientation.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct mapped_touch_slot_t {
  bool active;
  uint8_t vita_touch_id;
  int8_t chiaki_touch_id;
  uint16_t start_x;
  uint16_t start_y;
  bool moved;
} MappedTouchSlot;

#if VITARPS5_DEBUG_TOOLS
/* GH #458: a front touch is logged (INPUT_TEST/TOUCH) at most this often per finger while it
 * moves, and the motion sample (INPUT_TEST/MOTION) this often. Both only while the panel is open.
 */
#define DEBUG_TOUCH_LOG_MOVE_PERIOD_US 100000ULL
#define DEBUG_MOTION_LOG_PERIOD_US 500000ULL

/* What the front-touch loop did with one touch on its latest pass (INPUT_TEST/TOUCH fields). */
typedef struct debug_touch_resolve_t {
  int slot;        /* local touchpad slot it was sent on, -1 when not sent */
  int chiaki_id;   /* Chiaki touch id, -1 when not sent */
  bool sent;       /* a touchpad touch was sent for it */
  uint16_t sent_x; /* position sent, 1920x942 space (valid when sent) */
  uint16_t sent_y;
  int grid;           /* VitakiCtrlIn of the front grid zone (VITAKI_CTRL_IN_NONE if none) */
  int grid_out;       /* vcmi.in_out_btn[grid], 0 when none */
  bool to_touchpad;   /* the touch ended up mapped to the touchpad */
  unsigned int zones; /* FRONT_TOUCH_ZONE_* bits */
} DebugTouchResolve;

/* A touch no debug control or mapping has claimed: nothing sent, no zone. */
#define DEBUG_TOUCH_RESOLVE_UNSENT ((DebugTouchResolve){.slot = -1, .chiaki_id = -1})

/* GH #275: one front touch the debug-widget filter has seen. A touch is judged once, on the
 * frame its id first appears: swallowed if it started inside the widget rect, and then for its
 * whole life (sliding out does not hand it to the game); a touch that started outside stays the
 * game's even if it slides into the rect. Sized for the Vita's front-panel report limit.
 * GH #458: also keeps what is needed to log the touch (down, throttled moves, up). */
typedef struct debug_widget_touch_t {
  bool active;
  bool seen;
  DebugTouchTarget target; /* swallowed when not DEBUG_TOUCH_TARGET_NONE */
  uint8_t vita_touch_id;
  bool logged_down;     /* the "down" line has been written for this touch */
  uint64_t last_log_us; /* when the last line for this touch was written */
  int raw_x, raw_y;     /* latest raw sceTouch position */
  int logged_x, logged_y;
  DebugTouchResolve last; /* latest pass's outcome */
} DebugWidgetTouch;

#define DEBUG_WIDGET_TOUCH_SLOTS SCE_TOUCH_MAX_REPORT
#endif

#define CHIAKI_TOUCHPAD_WIDTH 1920
#define CHIAKI_TOUCHPAD_HEIGHT 942
#define TOUCHPAD_TAP_MOVE_THRESHOLD 24

_Static_assert(TOUCHPAD_GESTURE_SLOTS == CHIAKI_CONTROLLER_TOUCHES_MAX,
               "gesture tracker must follow every Chiaki touch slot");
_Static_assert(FRONT_TOUCH_ZONE_MAX_TOUCHES == SCE_TOUCH_MAX_REPORT,
               "front touch zone table must hold every touch the panel can report");

// Motion sensor deadband thresholds, in the same units sceMotionGetState()
// reports: acceleration in G (1.0 == Earth gravity), angularVelocity in
// rad/s, orientation as a unit quaternion. The Vita's MEMS accelerometer and
// gyro report measurement noise on the order of a few milli-G / milli-rad/s
// even perfectly still, and controller_state_equals_for_feedback_state()'s
// 1e-7 comparison epsilon treats any of that noise as a real input change on
// every single poll -- flooding the feedback sender. These thresholds sit an
// order of magnitude above typical MEMS noise floor so genuine tilt/gyro
// aiming input still registers well below perceptible input lag while
// stationary-hold noise is suppressed.
#define MOTION_ACCEL_DEADBAND 0.004f   // G
#define MOTION_GYRO_DEADBAND 0.002f    // rad/s
#define MOTION_ORIENT_DEADBAND 0.001f  // unit quaternion component

// Analog stick deadband, in the same units as the stored controller_state
// left_x/left_y/right_x/right_y fields: raw_adc is the Vita's 8-bit ADC
// reading (0-255) from sceCtrlPeekBufferPositive2(), converted below via
// (raw_adc - 128) * 2 * 0x7F, i.e. * 254. One raw ADC count of jitter
// therefore produces a delta of 254 in the stored int16_t value, and
// controller_state_equals_for_feedback_state() compares with no epsilon --
// so any 1-LSB ADC flicker while the stick is held registers as a "real"
// input change on every 2ms poll (ms_per_loop). This deadband and
// feedbacksender.c's FEEDBACK_STATE_TIMEOUT_MIN_MS floor are independent,
// complementary protections, not a hand-off from one to the other: this
// deadband suppresses ADC-jitter-driven sends so the floor engages less
// often in practice (fewer packets deferred, better latency in practice),
// while the floor remains the hard cap for genuine sustained stick motion
// that this deadband intentionally does not suppress. 512 is ~2 ADC counts
// of noise floor -- comfortably above single-LSB jitter -- while staying
// well under 2% of full stick travel (+/-~32512) and imperceptible against
// real analog input.
#define STICK_DEADBAND 512

static void set_ctrl_l2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in) {
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_l2 == ctrl_in) {
    stream->controller_state.l2_state = 0xff;
  } else {
    stream->controller_state.buttons |= vcmi.in_out_btn[ctrl_in];
  }
}

static void set_ctrl_r2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in) {
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_r2 == ctrl_in) {
    stream->controller_state.r2_state = 0xff;
  } else {
    stream->controller_state.buttons |= vcmi.in_out_btn[ctrl_in];
  }
}

static VitakiCtrlIn rear_grid_input_from_touch(int x, int y, int max_w, int max_h) {
  if (x < 0 || y < 0)
    return VITAKI_CTRL_IN_NONE;
  if (x >= max_w)
    x = max_w - 1;
  if (y >= max_h)
    y = max_h - 1;
  int col = (x * VITAKI_REAR_TOUCH_GRID_COLS) / max_w;
  int row = (y * VITAKI_REAR_TOUCH_GRID_ROWS) / max_h;
  if (col < 0)
    col = 0;
  if (col >= VITAKI_REAR_TOUCH_GRID_COLS)
    col = VITAKI_REAR_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_REAR_TOUCH_GRID_ROWS)
    row = VITAKI_REAR_TOUCH_GRID_ROWS - 1;
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + row * VITAKI_REAR_TOUCH_GRID_COLS +
                        col);
}

static uint16_t map_touchpad_x(int x, int max_x) {
  if (max_x <= 0)
    return 0;
  if (x < 0)
    x = 0;
  if (x > max_x)
    x = max_x;
  return (uint16_t)((x * (CHIAKI_TOUCHPAD_WIDTH - 1)) / max_x);
}

// Writes new_val into *dst only if it differs from the current value by at
// least `deadband`; otherwise keeps the old value ("hold last") so MEMS sensor
// noise below the deadband doesn't register as a controller_state change (see
// MOTION_*_DEADBAND above). Used for the accelerometer, where holding the
// last value is harmless. NaN inputs are naturally filtered out too:
// fabsf(NaN - x) is never >= deadband (all comparisons involving NaN are
// false per IEEE 754), so a single bad sample is dropped and the next good
// sample is free to update normally -- no permanent latch.
static inline void write_motion_sample(float *dst, float new_val, float deadband) {
  if (fabsf(new_val - *dst) >= deadband)
    *dst = new_val;
}

// Same as write_motion_sample(), but for a gyro axis: a new value within
// `deadband` of zero is written as exactly 0. Hold-last alone could leave an
// axis latched on a small non-zero rate (for example 0.0015 rad/s, settling
// slowly) for as long as the device is still, and the console integrates that
// rate as drift. Past the zero band it holds last like write_motion_sample().
// NaN is still dropped (both comparisons are false), so nothing latches.
static inline void write_motion_gyro(float *dst, float new_val, float deadband) {
  if (fabsf(new_val) < deadband)
    *dst = 0.0f;
  else
    write_motion_sample(dst, new_val, deadband);
}

// Same deadband contract as write_motion_sample(), but for the int16_t
// analog stick axes (see STICK_DEADBAND above): writes new_val into *dst
// only if it differs from the current value by at least `deadband`,
// otherwise keeps the old value so single-ADC-count jitter doesn't register
// as a controller_state change. Plain integer arithmetic -- these are exact
// ADC-derived values, not floats, so no fabsf()/NaN handling is needed.
static inline void write_stick_sample(int16_t *dst, int16_t new_val, int16_t deadband) {
  if (abs((int)new_val - (int)*dst) >= deadband)
    *dst = new_val;
}

// Same deadband contract as write_motion_sample(), but for all four
// quaternion components together: gates on whichever component moved the
// most instead of deadbanding each independently, so the four writes stay
// atomic (all from this poll's sample, or none of them). Independent
// per-component gating could otherwise mix components from different poll
// instants into a non-unit quaternion sent to the PS5.
//
// NaN handling differs from write_motion_sample() and is NOT self-healing
// here: per C11 7.12.12.2, fmaxf(a, NaN) returns the non-NaN argument `a`
// rather than NaN, so a NaN in any single component would be silently
// dropped from the max_delta chain below instead of poisoning it -- if the
// other three axes exceed the deadband on their own, the check would still
// pass and write a NaN into that component, persisting indefinitely if the
// device then holds still. Reject the whole sample up front if any
// component is non-finite; all four must be valid for the quaternion to be
// meaningful anyway.
static inline void write_motion_quat(float *dst_x, float *dst_y, float *dst_z, float *dst_w,
                                     float new_x, float new_y, float new_z, float new_w,
                                     float deadband) {
  if (!isfinite(new_x) || !isfinite(new_y) || !isfinite(new_z) || !isfinite(new_w))
    return;

  float max_delta = fabsf(new_x - *dst_x);
  max_delta = fmaxf(max_delta, fabsf(new_y - *dst_y));
  max_delta = fmaxf(max_delta, fabsf(new_z - *dst_z));
  max_delta = fmaxf(max_delta, fabsf(new_w - *dst_w));
  if (max_delta >= deadband) {
    *dst_x = new_x;
    *dst_y = new_y;
    *dst_z = new_z;
    *dst_w = new_w;
  }
}

// Feeds one fresh sceMotion sample (Vita frame) through the remap and the
// orientation tracker, then writes the tracker's DualSense-frame accel, gyro
// and orientation into `dst` through the deadband writers (so sensor noise
// does not change the controller state on every pass and flood the feedback
// sender). A sample with any non-finite value is dropped before the tracker
// sees it: one NaN would poison the Madgwick quaternion for good.
static void apply_motion_sample(ChiakiOrientationTracker *tracker, const SceMotionState *motion,
                                ChiakiControllerState *dst) {
  const float vita_accel[3] = {motion->acceleration.x, motion->acceleration.y,
                               motion->acceleration.z};
  const float vita_gyro[3] = {motion->angularVelocity.x, motion->angularVelocity.y,
                              motion->angularVelocity.z};
  for (int i = 0; i < 3; i++) {
    if (!isfinite(vita_accel[i]) || !isfinite(vita_gyro[i]))
      return;
  }
  float accel[3], gyro[3];
  motion_frame_accel_vita_to_dualsense(vita_accel, accel);
  motion_frame_gyro_vita_to_dualsense(vita_gyro, gyro);
  // The tracker takes microseconds as uint32 and handles the wrap.
  chiaki_orientation_tracker_update(tracker, gyro[0], gyro[1], gyro[2], accel[0], accel[1],
                                    accel[2], (uint32_t)motion->hostTimestamp);

  ChiakiControllerState tracked;
  chiaki_orientation_tracker_apply_to_controller_state(tracker, &tracked);
  write_motion_sample(&dst->accel_x, tracked.accel_x, MOTION_ACCEL_DEADBAND);
  write_motion_sample(&dst->accel_y, tracked.accel_y, MOTION_ACCEL_DEADBAND);
  write_motion_sample(&dst->accel_z, tracked.accel_z, MOTION_ACCEL_DEADBAND);
  write_motion_gyro(&dst->gyro_x, tracked.gyro_x, MOTION_GYRO_DEADBAND);
  write_motion_gyro(&dst->gyro_y, tracked.gyro_y, MOTION_GYRO_DEADBAND);
  write_motion_gyro(&dst->gyro_z, tracked.gyro_z, MOTION_GYRO_DEADBAND);
  write_motion_quat(&dst->orient_x, &dst->orient_y, &dst->orient_z, &dst->orient_w,
                    tracked.orient_x, tracked.orient_y, tracked.orient_z, tracked.orient_w,
                    MOTION_ORIENT_DEADBAND);
}

static uint16_t map_touchpad_y(int y, int max_y) {
  if (max_y <= 0)
    return 0;
  if (y < 0)
    y = 0;
  if (y > max_y)
    y = max_y;
  return (uint16_t)((y * (CHIAKI_TOUCHPAD_HEIGHT - 1)) / max_y);
}

#if VITARPS5_DEBUG_TOOLS
/* Judges one front-touch report against the debug targets (resync widget, Input test button,
 * Close pill). Returns true when the touch belongs to one of them and must be hidden from the
 * game (no touchpad event, no button mapping). The touch-down of a new swallowed touch raises
 * that target's one request; a held finger never repeats it because the touch is already known
 * on later frames. The panel body is not a target: touches under it reach the game.
 * *slot_out gets the touch's bookkeeping slot (NULL when every slot is taken). */
static bool debug_widget_filter_touch(DebugWidgetTouch *touches, uint8_t vita_touch_id, int x,
                                      int y, DebugWidgetTouch **slot_out) {
  DebugWidgetTouch *free_slot = NULL;
  *slot_out = NULL;
  for (int i = 0; i < DEBUG_WIDGET_TOUCH_SLOTS; i++) {
    if (touches[i].active && touches[i].vita_touch_id == vita_touch_id) {
      touches[i].seen = true;
      *slot_out = &touches[i];
      return touches[i].target != DEBUG_TOUCH_TARGET_NONE;
    }
    if (!touches[i].active && !free_slot)
      free_slot = &touches[i];
  }
  DebugTouchTarget target = debug_touch_target_at(x, y, context.stream.input_test_open);
  if (free_slot) {
    *free_slot = (DebugWidgetTouch){.active = true,
                                    .seen = true,
                                    .target = target,
                                    .vita_touch_id = vita_touch_id,
                                    .last = DEBUG_TOUCH_RESOLVE_UNSENT};
    *slot_out = free_slot;
  }
  switch (target) {
    case DEBUG_TOUCH_TARGET_RESYNC:
      context.stream.resync_tap_requested = true;
      break;
    case DEBUG_TOUCH_TARGET_INPUT_TEST:
      context.stream.input_test_toggle_request = DEBUG_PANEL_REQUEST_TOUCH;
      break;
    case DEBUG_TOUCH_TARGET_PANEL_CLOSE:
      context.stream.input_test_close_requested = true;
      break;
    case DEBUG_TOUCH_TARGET_NONE:
      break;
  }
  return target != DEBUG_TOUCH_TARGET_NONE;
}

/* Short name of a touch target for the INPUT_TEST/TOUCH line. */
static const char *debug_touch_target_name(DebugTouchTarget target) {
  switch (target) {
    case DEBUG_TOUCH_TARGET_RESYNC:
      return "resync";
    case DEBUG_TOUCH_TARGET_INPUT_TEST:
      return "input";
    case DEBUG_TOUCH_TARGET_PANEL_CLOSE:
      return "close";
    case DEBUG_TOUCH_TARGET_NONE:
      break;
  }
  return "none";
}

/* Writes one INPUT_TEST/TOUCH line for @p t from its latest recorded outcome. @p event is
 * "down", "move" or "up". An "up" line carries the last position and outcome seen. */
static void debug_log_touch(const DebugWidgetTouch *t, const char *event) {
  char sent[16] = "-";
  if (t->last.sent)
    snprintf(sent, sizeof(sent), "%u,%u", (unsigned)t->last.sent_x, (unsigned)t->last.sent_y);
  LOGD(
      "INPUT_TEST/TOUCH ev=%s vid=%u slot=%d cid=%d raw=%d,%d sent=%s zone=%d out=%d tp=%d "
      "zones=0x%02x swallowed=%s",
      event, (unsigned)t->vita_touch_id, t->last.slot, t->last.chiaki_id, t->raw_x, t->raw_y, sent,
      t->last.grid, t->last.grid_out, t->last.to_touchpad ? 1 : 0, t->last.zones,
      debug_touch_target_name(t->target));
}

/* Records one touch's latest outcome and writes its "down" line the first time it is seen, then
 * a "move" line at most every DEBUG_TOUCH_LOG_MOVE_PERIOD_US and only if the raw position
 * changed since the last line. Call only while the panel is open. */
static void debug_touch_note(DebugWidgetTouch *t, int x, int y, const DebugTouchResolve *resolve,
                             uint64_t now_us) {
  t->raw_x = x;
  t->raw_y = y;
  t->last = *resolve;
  const char *event;
  if (!t->logged_down) {
    event = "down";
  } else if (now_us - t->last_log_us >= DEBUG_TOUCH_LOG_MOVE_PERIOD_US &&
             (x != t->logged_x || y != t->logged_y)) {
    event = "move";
  } else {
    return;
  }
  t->logged_down = true;
  t->last_log_us = now_us;
  t->logged_x = x;
  t->logged_y = y;
  debug_log_touch(t, event);
}

/* Writes one INPUT_TEST/MOTION line: the raw sceMotion sample, then what the controller state
 * (after the deadbands) sends. */
static void debug_log_motion(const SceMotionState *motion, const ChiakiControllerState *sent) {
  LOGD(
      "INPUT_TEST/MOTION raw_accel=%.4f,%.4f,%.4f raw_gyro=%.4f,%.4f,%.4f "
      "raw_orient=%.4f,%.4f,%.4f,%.4f sent_accel=%.4f,%.4f,%.4f sent_gyro=%.4f,%.4f,%.4f "
      "sent_orient=%.4f,%.4f,%.4f,%.4f",
      motion->acceleration.x, motion->acceleration.y, motion->acceleration.z,
      motion->angularVelocity.x, motion->angularVelocity.y, motion->angularVelocity.z,
      motion->deviceQuat.x, motion->deviceQuat.y, motion->deviceQuat.z, motion->deviceQuat.w,
      sent->accel_x, sent->accel_y, sent->accel_z, sent->gyro_x, sent->gyro_y, sent->gyro_z,
      sent->orient_x, sent->orient_y, sent->orient_z, sent->orient_w);
}

/* Tracks the L + R + Select hold that toggles the Input test panel. Call once per pass with the
 * sampled Vita buttons and the time. Returns the buttons to use from here on: while all three
 * are held they are removed, so the game never sees them (from the first pass they are down, not
 * only after the hold completes). Raises one toggle request per hold; the buttons must be
 * released before another can fire. *held_ms gets the hold so far, capped at the threshold, or 0
 * when not held. */
static unsigned int debug_panel_combo_step(unsigned int buttons, uint64_t now_us,
                                           uint64_t *hold_start_us, bool *fired,
                                           uint32_t *held_ms) {
  const unsigned int combo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT;
  /* With Start also down it is the stream-exit combo; leave that alone. */
  if ((buttons & combo) != combo || (buttons & SCE_CTRL_START)) {
    *hold_start_us = 0;
    *fired = false;
    *held_ms = 0;
    return buttons;
  }
  if (!*hold_start_us)
    *hold_start_us = now_us;
  uint64_t elapsed_ms = (now_us - *hold_start_us) / 1000ULL;
  if (!*fired && elapsed_ms >= DEBUG_PANEL_COMBO_HOLD_MS) {
    *fired = true;
    context.stream.input_test_toggle_request = DEBUG_PANEL_REQUEST_COMBO;
  }
  *held_ms =
      elapsed_ms < DEBUG_PANEL_COMBO_HOLD_MS ? (uint32_t)elapsed_ms : DEBUG_PANEL_COMBO_HOLD_MS;
  return buttons & ~combo;
}

/* Publishes what was just handed to Chiaki, and the raw motion sample of the same pass, for the
 * Input test panel. See DebugInputSnapshot for the (deliberate) lack of locking. */
static void debug_publish_input_snapshot(const ChiakiControllerState *sent,
                                         const SceMotionState *motion, const InputTestClick *click,
                                         uint32_t combo_held_ms) {
  for (int i = 0; i < DEBUG_SNAPSHOT_TOUCHES; i++) {
    debug_input_snapshot.touches[i].active = sent->touches[i].id >= 0;
    debug_input_snapshot.touches[i].x = sent->touches[i].x;
    debug_input_snapshot.touches[i].y = sent->touches[i].y;
  }
  debug_input_snapshot.sent_accel[0] = sent->accel_x;
  debug_input_snapshot.sent_accel[1] = sent->accel_y;
  debug_input_snapshot.sent_accel[2] = sent->accel_z;
  debug_input_snapshot.sent_gyro[0] = sent->gyro_x;
  debug_input_snapshot.sent_gyro[1] = sent->gyro_y;
  debug_input_snapshot.sent_gyro[2] = sent->gyro_z;
  debug_input_snapshot.raw_accel[0] = motion->acceleration.x;
  debug_input_snapshot.raw_accel[1] = motion->acceleration.y;
  debug_input_snapshot.raw_accel[2] = motion->acceleration.z;
  debug_input_snapshot.raw_gyro[0] = motion->angularVelocity.x;
  debug_input_snapshot.raw_gyro[1] = motion->angularVelocity.y;
  debug_input_snapshot.raw_gyro[2] = motion->angularVelocity.z;
  debug_input_snapshot.click = *click;
  debug_input_snapshot.combo_held_ms = combo_held_ms;
}
#endif

void *host_input_thread_func(void *user) {
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, VITA_INPUT_THREAD_PRIORITY);
  sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, 0);

  sceMotionStartSampling();
  const int deadband = context.config.motion_deadband ? 1 : 0;
  const int deadband_ret = sceMotionSetDeadband(deadband);
  if (deadband_ret < 0)
    LOGE("sceMotionSetDeadband(%d) failed: 0x%08x", deadband, (unsigned)deadband_ret);
  else
    LOGD("Motion deadband applied: %d", deadband);
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  SceCtrlData ctrl;
  SceMotionState motion;
  ChiakiOrientationTracker orientation_tracker;
  chiaki_orientation_tracker_init(&orientation_tracker);
  bool motion_seen = false;
  uint64_t last_motion_timestamp = 0;
  VitaChiakiStream *stream = user;
  int ms_per_loop = 2;

  VitakiCtrlMapInfo vcmi = stream->vcmi;

  if (!vcmi.did_init)
    init_controller_map(&vcmi, context.config.controller_map_id);

  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_BACK);
  SceTouchData touch[SCE_TOUCH_PORT_MAX_NUM];
  MappedTouchSlot mapped_touch_slots[CHIAKI_CONTROLLER_TOUCHES_MAX] = {0};
#if VITARPS5_DEBUG_TOOLS
  DebugWidgetTouch widget_touches[DEBUG_WIDGET_TOUCH_SLOTS] = {0};
#endif
  int TOUCH_MAX_WIDTH = 1919;
  int TOUCH_MAX_HEIGHT = 1087;
  int TOUCH_MAX_WIDTH_BY_2 = TOUCH_MAX_WIDTH / 2;
  int TOUCH_MAX_HEIGHT_BY_2 = TOUCH_MAX_HEIGHT / 2;
  TouchpadGesture touchpad_gesture;
  touchpad_gesture_init(&touchpad_gesture);
  FrontTouchZones front_touch_zones;
  front_touch_zone_init(&front_touch_zones);
  for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++)
    mapped_touch_slots[slot_i].chiaki_touch_id = -1;

  bool vitaki_reartouch_left_l1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT_L1] != 0) ||
                                         (vcmi.in_l2 == VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
  bool vitaki_reartouch_right_r1_mapped =
      (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1] != 0) ||
      (vcmi.in_r2 == VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
  bool vitaki_select_start_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START] != 0);
  bool vitaki_left_square_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE] != 0) ||
                                   (vcmi.in_l2 == VITAKI_CTRL_IN_LEFT_SQUARE);
  bool vitaki_right_circle_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE] != 0) ||
                                    (vcmi.in_r2 == VITAKI_CTRL_IN_RIGHT_CIRCLE);

#if VITARPS5_DEBUG_TOOLS
  InputTestClick input_test_click;
  input_test_click_init(&input_test_click);
  uint64_t motion_log_us = 0;
  debug_tools_snapshot_reset();
  uint64_t panel_combo_start_us = 0;
  bool panel_combo_fired = false;
  uint32_t panel_combo_held_ms = 0;
#endif

  static int exit_combo_hold = 0;
  const int EXIT_COMBO_THRESHOLD = 500;
  static uint32_t controller_seq_counter = 0;
  const uint64_t INPUT_STALL_THRESHOLD_US = 300000;
  const uint64_t INPUT_STALL_LOG_INTERVAL_US = 1000000;

  if (context.stream.cached_controller_valid) {
    stream->controller_state = context.stream.cached_controller_state;
    context.stream.cached_controller_valid = false;
  }
  while (!stream->input_thread_should_exit) {
    bool controller_gate_open =
        stream->inputs_ready ||
        (stream->fast_restart_active && stream->session_init && !stream->stop_requested);

    if (!controller_gate_open) {
      uint64_t now_us = sceKernelGetProcessTimeWide();
      if (!context.stream.inputs_blocked_since_us)
        context.stream.inputs_blocked_since_us = now_us;
      uint64_t delta_since_block = now_us - context.stream.inputs_blocked_since_us;
      uint64_t delta_since_send =
          context.stream.last_input_packet_us ? (now_us - context.stream.last_input_packet_us) : 0;
      uint64_t observed = delta_since_send ? delta_since_send : delta_since_block;
      if (observed >= INPUT_STALL_THRESHOLD_US) {
        if (!context.stream.last_input_stall_log_us ||
            now_us - context.stream.last_input_stall_log_us >= INPUT_STALL_LOG_INTERVAL_US) {
          float ms = (float)observed / 1000.0f;
          LOGD("INPUT THREAD: controller packets waiting for Chiaki (%.2f ms since last activity)",
               ms);
          context.stream.last_input_stall_log_us = now_us;
        }
      }
    } else {
      context.stream.inputs_blocked_since_us = 0;
    }

    if (controller_gate_open) {
      uint64_t start_time_us = sceKernelGetProcessTimeWide();

      sceCtrlPeekBufferPositive(0, &ctrl, 1);

      bool exit_combo = (ctrl.buttons & SCE_CTRL_LTRIGGER) && (ctrl.buttons & SCE_CTRL_RTRIGGER) &&
                        (ctrl.buttons & SCE_CTRL_START);
      if (exit_combo && stream->session_init && !stream->stop_requested) {
        exit_combo_hold++;
        if (exit_combo_hold >= EXIT_COMBO_THRESHOLD) {
          host_request_stream_stop_from_input("L+R+Start");
          exit_combo_hold = 0;
          continue;
        }
      } else {
        exit_combo_hold = 0;
      }

#if VITARPS5_DEBUG_TOOLS
      ctrl.buttons = debug_panel_combo_step(ctrl.buttons, start_time_us, &panel_combo_start_us,
                                            &panel_combo_fired, &panel_combo_held_ms);
#endif

      if (stream->stop_requested) {
        usleep(ms_per_loop * 1000);
        continue;
      }

      for (int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
        sceTouchPeek(port, &touch[port], 1);
      }

      sceMotionGetState(&motion);
      // The sensor updates slower than this 2 ms poll; a repeated sample would count as a
      // warm-up sample with a zero time step in the tracker.
      if (!motion_seen || motion.hostTimestamp != last_motion_timestamp) {
        motion_seen = true;
        last_motion_timestamp = motion.hostTimestamp;
        apply_motion_sample(&orientation_tracker, &motion, &stream->controller_state);
      }

      write_stick_sample(&stream->controller_state.left_x, (ctrl.lx - 128) * 2 * 0x7F,
                         STICK_DEADBAND);
      write_stick_sample(&stream->controller_state.left_y, (ctrl.ly - 128) * 2 * 0x7F,
                         STICK_DEADBAND);
      write_stick_sample(&stream->controller_state.right_x, (ctrl.rx - 128) * 2 * 0x7F,
                         STICK_DEADBAND);
      write_stick_sample(&stream->controller_state.right_y, (ctrl.ry - 128) * 2 * 0x7F,
                         STICK_DEADBAND);

      stream->controller_state.buttons = 0x00;
      stream->controller_state.l2_state = 0x00;
      stream->controller_state.r2_state = 0x00;

      bool reartouch_right = false;
      bool reartouch_left = false;
      bool mapped_touch_seen[CHIAKI_CONTROLLER_TOUCHES_MAX] = {false};

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_BACK].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_BACK].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_BACK].report[touch_i].y;

        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_ANY];

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          reartouch_right = true;
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          reartouch_left = true;
        }

        VitakiCtrlIn grid_input =
            rear_grid_input_from_touch(x, y, TOUCH_MAX_WIDTH, TOUCH_MAX_HEIGHT);
        if (grid_input != VITAKI_CTRL_IN_NONE && grid_input < VITAKI_CTRL_IN_COUNT) {
          VitakiCtrlOut mapped = vcmi.in_out_btn[grid_input];
          if (mapped == VITAKI_CTRL_OUT_L2) {
            stream->controller_state.l2_state = 0xff;
          } else if (mapped == VITAKI_CTRL_OUT_R2) {
            stream->controller_state.r2_state = 0xff;
          } else if (mapped != VITAKI_CTRL_OUT_NONE) {
            stream->controller_state.buttons |= mapped;
          }
        }
      }

#if VITARPS5_DEBUG_TOOLS
      for (int w_i = 0; w_i < DEBUG_WIDGET_TOUCH_SLOTS; w_i++)
        widget_touches[w_i].seen = false;
#endif
      front_touch_zone_begin_pass(&front_touch_zones);

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_FRONT].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].y;
        uint8_t vita_touch_id = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].id;
#if VITARPS5_DEBUG_TOOLS
        DebugWidgetTouch *dbg_touch = NULL;
        if (debug_widget_filter_touch(widget_touches, vita_touch_id, x, y, &dbg_touch)) {
          if (dbg_touch && context.stream.input_test_open) {
            DebugTouchResolve swallowed = DEBUG_TOUCH_RESOLVE_UNSENT;
            debug_touch_note(dbg_touch, x, y, &swallowed, start_time_us);
          }
          continue;
        }
        DebugTouchResolve dbg_resolve = DEBUG_TOUCH_RESOLVE_UNSENT;
#endif
        // GH #460: the touch keeps the outcome of the zone it started in until it lifts.
        const FrontTouchOutcome *zone_outcome = front_touch_zone_latch(
            &front_touch_zones, vita_touch_id, x, y, TOUCH_MAX_WIDTH, TOUCH_MAX_HEIGHT, &vcmi);
        if (!zone_outcome)
          continue;
        stream->controller_state.buttons |= zone_outcome->buttons;
        if (zone_outcome->l2)
          stream->controller_state.l2_state = 0xff;
        if (zone_outcome->r2)
          stream->controller_state.r2_state = 0xff;
        bool mapped_to_touchpad = zone_outcome->to_touchpad;
#if VITARPS5_DEBUG_TOOLS
        dbg_resolve.grid = zone_outcome->grid;
        dbg_resolve.grid_out = zone_outcome->grid_out;
        dbg_resolve.zones = zone_outcome->zones;
#endif

        if (mapped_to_touchpad) {
          uint16_t touchpad_x = map_touchpad_x(x, TOUCH_MAX_WIDTH);
          uint16_t touchpad_y = map_touchpad_y(y, TOUCH_MAX_HEIGHT);
          int slot_index = -1;

          for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++) {
            if (mapped_touch_slots[slot_i].active &&
                mapped_touch_slots[slot_i].vita_touch_id == vita_touch_id) {
              slot_index = slot_i;
              break;
            }
          }

          if (slot_index < 0) {
            for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++) {
              if (!mapped_touch_slots[slot_i].active) {
                int8_t chiaki_touch_id = chiaki_controller_state_start_touch(
                    &stream->controller_state, touchpad_x, touchpad_y);
                if (chiaki_touch_id >= 0) {
                  mapped_touch_slots[slot_i].active = true;
                  mapped_touch_slots[slot_i].vita_touch_id = vita_touch_id;
                  mapped_touch_slots[slot_i].chiaki_touch_id = chiaki_touch_id;
                  mapped_touch_slots[slot_i].start_x = touchpad_x;
                  mapped_touch_slots[slot_i].start_y = touchpad_y;
                  mapped_touch_slots[slot_i].moved = false;
                  slot_index = slot_i;
                }
                // Only one local slot can be claimed for this Vita touch in this frame.
                // If Chiaki can't allocate an ID here, trying other free local slots
                // in the same frame won't change that outcome.
                break;
              }
            }
          } else {
            int dx = (int)touchpad_x - (int)mapped_touch_slots[slot_index].start_x;
            int dy = (int)touchpad_y - (int)mapped_touch_slots[slot_index].start_y;
            if (dx < 0)
              dx = -dx;
            if (dy < 0)
              dy = -dy;
            if (dx > TOUCHPAD_TAP_MOVE_THRESHOLD || dy > TOUCHPAD_TAP_MOVE_THRESHOLD)
              mapped_touch_slots[slot_index].moved = true;
            chiaki_controller_state_set_touch_pos(
                &stream->controller_state, (uint8_t)mapped_touch_slots[slot_index].chiaki_touch_id,
                touchpad_x, touchpad_y);
          }

          if (slot_index >= 0) {
            mapped_touch_seen[slot_index] = true;
#if VITARPS5_DEBUG_TOOLS
            dbg_resolve.sent = true;
            dbg_resolve.sent_x = touchpad_x;
            dbg_resolve.sent_y = touchpad_y;
            dbg_resolve.slot = slot_index;
            dbg_resolve.chiaki_id = mapped_touch_slots[slot_index].chiaki_touch_id;
#endif
          }
        }

#if VITARPS5_DEBUG_TOOLS
        if (dbg_touch && context.stream.input_test_open) {
          dbg_resolve.to_touchpad = mapped_to_touchpad;
          debug_touch_note(dbg_touch, x, y, &dbg_resolve, start_time_us);
        }
#endif
      }

      front_touch_zone_end_pass(&front_touch_zones);

#if VITARPS5_DEBUG_TOOLS
      for (int w_i = 0; w_i < DEBUG_WIDGET_TOUCH_SLOTS; w_i++) {
        DebugWidgetTouch *lifted = &widget_touches[w_i];
        if (lifted->seen || !lifted->active)
          continue;
        if (lifted->logged_down && context.stream.input_test_open)
          debug_log_touch(lifted, "up");
        lifted->active = false;
        lifted->logged_down = false;
      }
#endif

      // GH #454: a lifted tap keeps its Chiaki touch alive while the click is held, so the
      // console sees the click with a finger on the pad. The gesture tracker says when to let go.
      TouchpadGestureInput gesture_in[TOUCHPAD_GESTURE_SLOTS];
      TouchpadGestureOutput gesture_out;
      for (int slot_i = 0; slot_i < TOUCHPAD_GESTURE_SLOTS; slot_i++) {
        gesture_in[slot_i].active = mapped_touch_slots[slot_i].active;
        gesture_in[slot_i].seen = mapped_touch_seen[slot_i];
        gesture_in[slot_i].moved = mapped_touch_slots[slot_i].moved;
      }
      touchpad_gesture_step(&touchpad_gesture, sceKernelGetProcessTimeWide(), gesture_in,
                            &gesture_out);
      if (gesture_out.click)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;

      for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++) {
        if (mapped_touch_slots[slot_i].active && gesture_out.release[slot_i]) {
          if (mapped_touch_slots[slot_i].chiaki_touch_id >= 0) {
            chiaki_controller_state_stop_touch(&stream->controller_state,
                                               (uint8_t)mapped_touch_slots[slot_i].chiaki_touch_id);
          }
          mapped_touch_slots[slot_i].active = false;
          mapped_touch_slots[slot_i].vita_touch_id = 0;
          mapped_touch_slots[slot_i].chiaki_touch_id = -1;
          mapped_touch_slots[slot_i].start_x = 0;
          mapped_touch_slots[slot_i].start_y = 0;
          mapped_touch_slots[slot_i].moved = false;
        }
      }

      if (ctrl.buttons & SCE_CTRL_SELECT)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE;
      if (ctrl.buttons & SCE_CTRL_START)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
      if (ctrl.buttons & SCE_CTRL_UP)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
      if (ctrl.buttons & SCE_CTRL_RIGHT)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
      if (ctrl.buttons & SCE_CTRL_DOWN)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
      if (ctrl.buttons & SCE_CTRL_LEFT)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
      if (ctrl.buttons & SCE_CTRL_TRIANGLE)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
      if (ctrl.buttons & SCE_CTRL_CIRCLE)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
      if (ctrl.buttons & SCE_CTRL_CROSS)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
      if (ctrl.buttons & SCE_CTRL_SQUARE)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
      if (ctrl.buttons & SCE_CTRL_L3)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
      if (ctrl.buttons & SCE_CTRL_R3)
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;

      if (ctrl.buttons & SCE_CTRL_LTRIGGER) {
        if (reartouch_left && vitaki_reartouch_left_l1_mapped) {
          // Rear-touch path has no touch-coordinate emission; keep button semantics.
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
        } else {
          // Non-front path: TOUCHPAD outputs (if configured) are treated as button bits.
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_L1);
        }
      }
      if (ctrl.buttons & SCE_CTRL_RTRIGGER) {
        if (reartouch_right && vitaki_reartouch_right_r1_mapped) {
          // Rear-touch path has no touch-coordinate emission; keep button semantics.
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
        } else {
          // Non-front path: TOUCHPAD outputs (if configured) are treated as button bits.
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_R1);
        }
      }

      if (vitaki_select_start_mapped) {
        if ((ctrl.buttons & SCE_CTRL_SELECT) && (ctrl.buttons & SCE_CTRL_START)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_SHARE;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_OPTIONS;
          stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START];
        }
      }

      if (vitaki_left_square_mapped) {
        if ((ctrl.buttons & SCE_CTRL_LEFT) && (ctrl.buttons & SCE_CTRL_SQUARE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_BOX;
          // Combo path is button-only; no touchpad coordinate emission here.
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_LEFT_SQUARE);
        }
      }

      if (vitaki_right_circle_mapped) {
        if ((ctrl.buttons & SCE_CTRL_RIGHT) && (ctrl.buttons & SCE_CTRL_CIRCLE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_MOON;
          // Combo path is button-only; no touchpad coordinate emission here.
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_RIGHT_CIRCLE);
        }
      }

      chiaki_session_set_controller_state_ts(&stream->session, &stream->controller_state,
                                             start_time_us);
#if VITARPS5_DEBUG_TOOLS
      bool click_was_down = input_test_click.down;
      input_test_click_step(
          &input_test_click,
          (stream->controller_state.buttons & CHIAKI_CONTROLLER_BUTTON_TOUCHPAD) != 0,
          sceKernelGetProcessTimeWide());
      if (context.stream.input_test_open) {
        if (click_was_down && !input_test_click.down) {
          LOGD("INPUT_TEST/CLICK count=%u hold_ms=%u", (unsigned)input_test_click.count,
               (unsigned)input_test_click.last_hold_ms);
        }
        if (start_time_us - motion_log_us >= DEBUG_MOTION_LOG_PERIOD_US) {
          motion_log_us = start_time_us;
          debug_log_motion(&motion, &stream->controller_state);
        }
      }
      debug_publish_input_snapshot(&stream->controller_state, &motion, &input_test_click,
                                   panel_combo_held_ms);
#endif
      context.stream.cached_controller_state = stream->controller_state;
      context.stream.cached_controller_valid = true;
      context.stream.last_input_packet_us = sceKernelGetProcessTimeWide();
      context.stream.last_input_stall_log_us = 0;
      controller_seq_counter++;
      if ((controller_seq_counter % 500) == 0) {
        LOGD("Controller send seq %u (Vita)", controller_seq_counter);
      }

      uint64_t diff_time_us = sceKernelGetProcessTimeWide() - start_time_us;
      uint64_t loop_budget_us = (uint64_t)ms_per_loop * 1000ULL;
      if (diff_time_us < loop_budget_us)
        usleep((useconds_t)(loop_budget_us - diff_time_us));

    } else {
      usleep(1000);
    }
  }

  return 0;
}
