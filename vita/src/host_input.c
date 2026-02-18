#include "context.h"
#include "host_input.h"

#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <unistd.h>

typedef struct mapped_touch_slot_t {
  bool active;
  uint8_t vita_touch_id;
  int8_t chiaki_touch_id;
  uint16_t start_x;
  uint16_t start_y;
  bool moved;
} MappedTouchSlot;

#define CHIAKI_TOUCHPAD_WIDTH 1920
#define CHIAKI_TOUCHPAD_HEIGHT 942
#define TOUCHPAD_TAP_MOVE_THRESHOLD 24
#define TOUCHPAD_CLICK_PULSE_FRAMES 2
#define PS_DOUBLE_PRESS_WINDOW_US 300000ULL

static bool set_ps_button_intercept_state(bool enabled) {
#if defined(SCE_CTRL_PSBUTTON)
  int ret = sceCtrlSetButtonIntercept(enabled ? 1 : 0);
  if (ret < 0) {
    LOGE("Failed to set PS button intercept=%d (0x%x)", enabled ? 1 : 0, ret);
    return false;
  }
  return true;
#else
  (void)enabled;
  return false;
#endif
}

// If mapped_to_touchpad is non-NULL, TOUCHPAD outputs are routed to the
// touch-event path instead of being OR'd into button bits.
static void set_ctrl_l2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in, bool *mapped_to_touchpad) {
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_l2 == ctrl_in) {
    stream->controller_state.l2_state = 0xff;
  } else {
    VitakiCtrlOut out = vcmi.in_out_btn[ctrl_in];
    if (mapped_to_touchpad && out == VITAKI_CTRL_OUT_TOUCHPAD) {
      *mapped_to_touchpad = true;
    } else {
      stream->controller_state.buttons |= out;
    }
  }
}

// If mapped_to_touchpad is non-NULL, TOUCHPAD outputs are routed to the
// touch-event path instead of being OR'd into button bits.
static void set_ctrl_r2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in, bool *mapped_to_touchpad) {
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_r2 == ctrl_in) {
    stream->controller_state.r2_state = 0xff;
  } else {
    VitakiCtrlOut out = vcmi.in_out_btn[ctrl_in];
    if (mapped_to_touchpad && out == VITAKI_CTRL_OUT_TOUCHPAD) {
      *mapped_to_touchpad = true;
    } else {
      stream->controller_state.buttons |= out;
    }
  }
}

static VitakiCtrlIn front_grid_input_from_touch(int x, int y, int max_w, int max_h) {
  if (x < 0 || y < 0)
    return VITAKI_CTRL_IN_NONE;
  if (x >= max_w)
    x = max_w - 1;
  if (y >= max_h)
    y = max_h - 1;
  int col = (x * VITAKI_FRONT_TOUCH_GRID_COLS) / max_w;
  int row = (y * VITAKI_FRONT_TOUCH_GRID_ROWS) / max_h;
  if (col < 0)
    col = 0;
  if (col >= VITAKI_FRONT_TOUCH_GRID_COLS)
    col = VITAKI_FRONT_TOUCH_GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= VITAKI_FRONT_TOUCH_GRID_ROWS)
    row = VITAKI_FRONT_TOUCH_GRID_ROWS - 1;
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_FRONTTOUCH_GRID_START + row * VITAKI_FRONT_TOUCH_GRID_COLS + col);
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
  return (VitakiCtrlIn)(VITAKI_CTRL_IN_REARTOUCH_GRID_START + row * VITAKI_REAR_TOUCH_GRID_COLS + col);
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

static uint16_t map_touchpad_y(int y, int max_y) {
  if (max_y <= 0)
    return 0;
  if (y < 0)
    y = 0;
  if (y > max_y)
    y = max_y;
  return (uint16_t)((y * (CHIAKI_TOUCHPAD_HEIGHT - 1)) / max_y);
}

void *host_input_thread_func(void* user) {
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 96);
  sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, 0);

  sceMotionStartSampling();
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  SceCtrlData ctrl;
  SceMotionState motion;
  VitaChiakiStream *stream = user;
  int ms_per_loop = 2;

  VitakiCtrlMapInfo vcmi = stream->vcmi;

  if (!vcmi.did_init) init_controller_map(&vcmi, context.config.controller_map_id);

  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);
  sceTouchEnableTouchForce(SCE_TOUCH_PORT_BACK);
  SceTouchData touch[SCE_TOUCH_PORT_MAX_NUM];
  MappedTouchSlot mapped_touch_slots[CHIAKI_CONTROLLER_TOUCHES_MAX] = { 0 };
  int TOUCH_MAX_WIDTH = 1919;
  int TOUCH_MAX_HEIGHT = 1087;
  int TOUCH_MAX_WIDTH_BY_2 = TOUCH_MAX_WIDTH/2;
  int TOUCH_MAX_HEIGHT_BY_2 = TOUCH_MAX_HEIGHT/2;
  int TOUCH_MAX_WIDTH_BY_4 = TOUCH_MAX_WIDTH/4;
  int TOUCH_MAX_HEIGHT_BY_4 = TOUCH_MAX_HEIGHT/4;
  int FRONT_ARC_RADIUS = TOUCH_MAX_HEIGHT/3;
  int FRONT_ARC_RADIUS_2 = FRONT_ARC_RADIUS*FRONT_ARC_RADIUS;
  uint8_t pending_touchpad_click_frames = 0;
  for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++)
    mapped_touch_slots[slot_i].chiaki_touch_id = -1;

  bool vitaki_reartouch_left_l1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT_L1] != 0) || (vcmi.in_l2 == VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
  bool vitaki_reartouch_right_r1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1] != 0) || (vcmi.in_r2 == VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
  bool vitaki_select_start_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START] != 0);
  bool vitaki_left_square_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE] != 0) || (vcmi.in_l2 == VITAKI_CTRL_IN_LEFT_SQUARE);
  bool vitaki_right_circle_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE] != 0) || (vcmi.in_r2 == VITAKI_CTRL_IN_RIGHT_CIRCLE);

  static int exit_combo_hold = 0;
  const int EXIT_COMBO_THRESHOLD = 500;
  static uint32_t controller_seq_counter = 0;
  const uint64_t INPUT_STALL_THRESHOLD_US = 300000;
  const uint64_t INPUT_STALL_LOG_INTERVAL_US = 1000000;
  bool ps_button_dual_mode_active = false;
  bool ps_intercept_enabled = false;
  int ps_intercept_original = 0;
  bool ps_prev_down = false;
  bool ps_waiting_second_tap = false;
  bool ps_passthrough_active = false;
  uint64_t ps_first_release_us = 0;

#if defined(SCE_CTRL_PSBUTTON)
  if (sceCtrlGetButtonIntercept(&ps_intercept_original) < 0)
    ps_intercept_original = 0;
#endif

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
      uint64_t delta_since_block =
          now_us - context.stream.inputs_blocked_since_us;
      uint64_t delta_since_send =
          context.stream.last_input_packet_us ?
          (now_us - context.stream.last_input_packet_us) : 0;
      uint64_t observed = delta_since_send ? delta_since_send : delta_since_block;
      if (observed >= INPUT_STALL_THRESHOLD_US) {
        if (!context.stream.last_input_stall_log_us ||
            now_us - context.stream.last_input_stall_log_us >= INPUT_STALL_LOG_INTERVAL_US) {
          float ms = (float)observed / 1000.0f;
          LOGD("INPUT THREAD: controller packets waiting for Chiaki (%.2f ms since last activity)", ms);
          context.stream.last_input_stall_log_us = now_us;
        }
      }
    } else {
      context.stream.inputs_blocked_since_us = 0;
    }

    if (controller_gate_open) {
      if (context.config.ps_button_dual_mode && !ps_button_dual_mode_active) {
        ps_button_dual_mode_active = set_ps_button_intercept_state(true);
        ps_intercept_enabled = ps_button_dual_mode_active;
        if (!ps_button_dual_mode_active) {
          LOGE("PS button dual mode requested but intercept could not be enabled");
        }
      } else if (!context.config.ps_button_dual_mode && ps_button_dual_mode_active) {
        set_ps_button_intercept_state(ps_intercept_original != 0);
        ps_intercept_enabled = (ps_intercept_original != 0);
        ps_button_dual_mode_active = false;
        ps_waiting_second_tap = false;
        ps_passthrough_active = false;
        ps_first_release_us = 0;
        ps_prev_down = false;
      }

      uint64_t start_time_us = sceKernelGetProcessTimeWide();

      sceCtrlPeekBufferPositiveExt(0, &ctrl, 1);

      bool exit_combo = (ctrl.buttons & SCE_CTRL_LTRIGGER) &&
                        (ctrl.buttons & SCE_CTRL_RTRIGGER) &&
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

      if (stream->stop_requested) {
        usleep(ms_per_loop * 1000);
        continue;
      }

      for(int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
        sceTouchPeek(port, &touch[port], 1);
      }

      sceMotionGetState(&motion);
      stream->controller_state.accel_x = motion.acceleration.x;
      stream->controller_state.accel_y = motion.acceleration.y;
      stream->controller_state.accel_z = motion.acceleration.z;

      stream->controller_state.orient_x = motion.deviceQuat.x;
      stream->controller_state.orient_y = motion.deviceQuat.y;
      stream->controller_state.orient_z = motion.deviceQuat.z;
      stream->controller_state.orient_w = motion.deviceQuat.w;

      stream->controller_state.gyro_x = motion.angularVelocity.x;
      stream->controller_state.gyro_y = motion.angularVelocity.y;
      stream->controller_state.gyro_z = motion.angularVelocity.z;

      stream->controller_state.left_x = (ctrl.lx - 128) * 2 * 0x7F;
      stream->controller_state.left_y = (ctrl.ly - 128) * 2 * 0x7F;
      stream->controller_state.right_x = (ctrl.rx - 128) * 2 * 0x7F;
      stream->controller_state.right_y = (ctrl.ry - 128) * 2 * 0x7F;

      stream->controller_state.buttons = 0x00;
      stream->controller_state.l2_state = 0x00;
      stream->controller_state.r2_state = 0x00;

#if defined(SCE_CTRL_PSBUTTON)
      if (ps_button_dual_mode_active) {
        uint64_t now_us = sceKernelGetProcessTimeWide();
        bool ps_down = (ctrl.buttons & SCE_CTRL_PSBUTTON) != 0;
        bool ps_pressed = ps_down && !ps_prev_down;
        bool ps_released = !ps_down && ps_prev_down;

        if (!ps_waiting_second_tap && ps_released) {
          // First tap completed: start second-tap window with intercept still enabled.
          ps_waiting_second_tap = true;
          ps_first_release_us = now_us;
          LOGD("PS dual mode: first tap armed");
        } else if (ps_waiting_second_tap && !ps_passthrough_active && ps_pressed &&
                   (now_us - ps_first_release_us) <= PS_DOUBLE_PRESS_WINDOW_US) {
          // Second press within window: open local passthrough while button is held.
          if (ps_intercept_enabled && set_ps_button_intercept_state(false)) {
            ps_intercept_enabled = false;
            ps_passthrough_active = true;
            LOGD("PS dual mode: second press detected, passthrough active");
          } else {
            // Could not open passthrough, fall back to remote PS for this interaction.
            stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
            ps_waiting_second_tap = false;
            ps_passthrough_active = false;
            ps_first_release_us = 0;
            LOGE("PS dual mode: failed to open second-press passthrough, sending remote PS");
          }
        }

        if (ps_waiting_second_tap && ps_passthrough_active && ps_released) {
          // Double tap finished: close passthrough and restore intercept.
          ps_waiting_second_tap = false;
          ps_passthrough_active = false;
          ps_first_release_us = 0;
          LOGD("PS dual mode: double tap confirmed (Vita local), restoring intercept");
          if (!ps_intercept_enabled && context.config.ps_button_dual_mode) {
            ps_intercept_enabled = set_ps_button_intercept_state(true);
          }
        } else if (ps_waiting_second_tap && !ps_passthrough_active && !ps_down &&
                   (now_us - ps_first_release_us) > PS_DOUBLE_PRESS_WINDOW_US) {
          // Single tap timeout: emit remote PS once.
          stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
          ps_waiting_second_tap = false;
          ps_passthrough_active = false;
          ps_first_release_us = 0;
          LOGD("PS dual mode: single tap timeout, sending remote PS");
          if (!ps_intercept_enabled && context.config.ps_button_dual_mode) {
            ps_intercept_enabled = set_ps_button_intercept_state(true);
          }
        } else if (!ps_intercept_enabled && !ps_passthrough_active && !ps_down &&
                   context.config.ps_button_dual_mode) {
          ps_intercept_enabled = set_ps_button_intercept_state(true);
        }

        ps_prev_down = ps_down;
      }
#endif

      if (pending_touchpad_click_frames > 0) {
        stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
        pending_touchpad_click_frames--;
      }

      bool reartouch_right = false;
      bool reartouch_left = false;
      bool mapped_touch_seen[CHIAKI_CONTROLLER_TOUCHES_MAX] = { false };

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_BACK].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_BACK].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_BACK].report[touch_i].y;

        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_ANY];

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          reartouch_right = true;
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          reartouch_left = true;
        }

        VitakiCtrlIn grid_input = rear_grid_input_from_touch(x, y, TOUCH_MAX_WIDTH, TOUCH_MAX_HEIGHT);
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

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_FRONT].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].y;
        uint8_t vita_touch_id = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].id;
        bool mapped_to_touchpad = false;
        if (vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY] == VITAKI_CTRL_OUT_TOUCHPAD) {
          mapped_to_touchpad = true;
        } else {
          stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY];
        }

        VitakiCtrlIn grid_input = front_grid_input_from_touch(x, y, TOUCH_MAX_WIDTH, TOUCH_MAX_HEIGHT);
        if (grid_input != VITAKI_CTRL_IN_NONE) {
          VitakiCtrlOut mapped = vcmi.in_out_btn[grid_input];
          if (mapped == VITAKI_CTRL_OUT_L2) {
            stream->controller_state.l2_state = 0xff;
          } else if (mapped == VITAKI_CTRL_OUT_R2) {
            stream->controller_state.r2_state = 0xff;
          } else if (mapped == VITAKI_CTRL_OUT_TOUCHPAD) {
            mapped_to_touchpad = true;
          } else if (mapped != VITAKI_CTRL_OUT_NONE) {
            stream->controller_state.buttons |= mapped;
          }
        }

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_RIGHT, &mapped_to_touchpad);

          if (y*y + (x-TOUCH_MAX_WIDTH)*(x-TOUCH_MAX_WIDTH) <= FRONT_ARC_RADIUS_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC, &mapped_to_touchpad);
          } else if ((y-TOUCH_MAX_HEIGHT)*(y-TOUCH_MAX_HEIGHT) + (x-TOUCH_MAX_WIDTH)*(x-TOUCH_MAX_WIDTH) <= FRONT_ARC_RADIUS_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC, &mapped_to_touchpad);
          }
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LEFT, &mapped_to_touchpad);

          if (y*y + x*x <= FRONT_ARC_RADIUS_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC, &mapped_to_touchpad);
          } else if ((y-TOUCH_MAX_HEIGHT)*(y-TOUCH_MAX_HEIGHT) + x*x <= FRONT_ARC_RADIUS_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC, &mapped_to_touchpad);
          }
        }

        if ((x >= TOUCH_MAX_WIDTH_BY_4) && (x <= TOUCH_MAX_WIDTH - TOUCH_MAX_WIDTH_BY_4)
            && (y >= TOUCH_MAX_HEIGHT_BY_4) && (y <= TOUCH_MAX_HEIGHT - TOUCH_MAX_HEIGHT_BY_4)
            ) {
          if (vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER] == VITAKI_CTRL_OUT_TOUCHPAD) {
            mapped_to_touchpad = true;
          } else {
            stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER];
          }
        }

        if (mapped_to_touchpad) {
          uint16_t touchpad_x = map_touchpad_x(x, TOUCH_MAX_WIDTH);
          uint16_t touchpad_y = map_touchpad_y(y, TOUCH_MAX_HEIGHT);
          int slot_index = -1;

          for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++) {
            if (mapped_touch_slots[slot_i].active && mapped_touch_slots[slot_i].vita_touch_id == vita_touch_id) {
              slot_index = slot_i;
              break;
            }
          }

          if (slot_index < 0) {
            for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++) {
              if (!mapped_touch_slots[slot_i].active) {
                int8_t chiaki_touch_id = chiaki_controller_state_start_touch(&stream->controller_state,
                                                                              touchpad_x,
                                                                              touchpad_y);
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
            chiaki_controller_state_set_touch_pos(&stream->controller_state,
                                                  (uint8_t)mapped_touch_slots[slot_index].chiaki_touch_id,
                                                  touchpad_x,
                                                  touchpad_y);
          }

          if (slot_index >= 0)
            mapped_touch_seen[slot_index] = true;
        }
      }

      for (int slot_i = 0; slot_i < CHIAKI_CONTROLLER_TOUCHES_MAX; slot_i++) {
        if (mapped_touch_slots[slot_i].active && !mapped_touch_seen[slot_i]) {
          if (!mapped_touch_slots[slot_i].moved && pending_touchpad_click_frames < TOUCHPAD_CLICK_PULSE_FRAMES)
            pending_touchpad_click_frames = TOUCHPAD_CLICK_PULSE_FRAMES;
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

      if (ctrl.buttons & SCE_CTRL_SELECT)   stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_SHARE;
      if (ctrl.buttons & SCE_CTRL_START)    stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
      if (ctrl.buttons & SCE_CTRL_UP)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
      if (ctrl.buttons & SCE_CTRL_RIGHT)    stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
      if (ctrl.buttons & SCE_CTRL_DOWN)     stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
      if (ctrl.buttons & SCE_CTRL_LEFT)     stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
      if (ctrl.buttons & SCE_CTRL_TRIANGLE) stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
      if (ctrl.buttons & SCE_CTRL_CIRCLE)   stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
      if (ctrl.buttons & SCE_CTRL_CROSS)    stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
      if (ctrl.buttons & SCE_CTRL_SQUARE)   stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
      if (ctrl.buttons & SCE_CTRL_L3)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
      if (ctrl.buttons & SCE_CTRL_R3)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;

      if (ctrl.buttons & SCE_CTRL_LTRIGGER) {
        if (reartouch_left && vitaki_reartouch_left_l1_mapped) {
          // Rear-touch path has no touch-coordinate emission; keep button semantics.
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LEFT_L1, NULL);
        } else {
          // Non-front path: TOUCHPAD outputs (if configured) are treated as button bits.
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_L1, NULL);
        }
      }
      if (ctrl.buttons & SCE_CTRL_RTRIGGER) {
        if (reartouch_right && vitaki_reartouch_right_r1_mapped) {
          // Rear-touch path has no touch-coordinate emission; keep button semantics.
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1, NULL);
        } else {
          // Non-front path: TOUCHPAD outputs (if configured) are treated as button bits.
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_R1, NULL);
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
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_LEFT_SQUARE, NULL);
        }
      }

      if (vitaki_right_circle_mapped) {
        if ((ctrl.buttons & SCE_CTRL_RIGHT) && (ctrl.buttons & SCE_CTRL_CIRCLE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_MOON;
          // Combo path is button-only; no touchpad coordinate emission here.
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_RIGHT_CIRCLE, NULL);
        }
      }

      chiaki_session_set_controller_state(&stream->session, &stream->controller_state);
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

#if defined(SCE_CTRL_PSBUTTON)
  if (ps_intercept_enabled) {
    set_ps_button_intercept_state(ps_intercept_original != 0);
  } else if (ps_button_dual_mode_active && ps_intercept_original != 0) {
    set_ps_button_intercept_state(true);
  }
#endif

  return 0;
}
