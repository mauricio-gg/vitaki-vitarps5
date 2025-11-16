#include "context.h"
#include "config.h"
#include "controller.h"
#include "host.h"
#include "discovery.h"
#include "audio.h"
#include "video.h"
#include "string.h"
#include <stdio.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <chiaki/base64.h>
#include <chiaki/session.h>
#include <chiaki/videoreceiver.h>
#include <chiaki/frameprocessor.h>

static void reset_stream_metrics(void);
static void update_latency_metrics(void);
static unsigned int latency_mode_target_kbps(VitaChiakiLatencyMode mode);
static void apply_latency_mode(ChiakiConnectVideoProfile *profile, VitaChiakiLatencyMode mode);
static void request_stream_stop(const char *reason);
static void resume_discovery_if_needed(void);

#define STREAM_RETRY_COOLDOWN_US (3 * 1000 * 1000ULL)

void host_free(VitaChiakiHost *host) {
  if (host) {
    if (host->discovery_state) {
      free(host->discovery_state);
    }
    if (host->registered_state) {
      free(host->registered_state);
    }
    if (host->hostname) {
      free(host->hostname);
    }
  }
}

ChiakiRegist regist = {};
static void regist_cb(ChiakiRegistEvent *event, void *user) {
  LOGD("regist event %d", event->type);
  if (event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS) {
    context.active_host->type |= REGISTERED;

    if (context.active_host->registered_state != NULL) {
      free(context.active_host->registered_state);
      context.active_host->registered_state = event->registered_host;
      memcpy(&context.active_host->server_mac, &(event->registered_host->server_mac), 6);
      printf("FOUND HOST TO UPDATE\n");
      for (int rhost_idx = 0; rhost_idx < context.config.num_registered_hosts; rhost_idx++) {
        VitaChiakiHost* rhost =
            context.config.registered_hosts[rhost_idx];
        if (rhost == NULL) {
          continue;
        }

        printf("NAME1 %s\n", rhost->registered_state->server_nickname);
        printf("NAME2 %s\n", context.active_host->registered_state->server_nickname);
        if ((rhost->server_mac) && (context.active_host->server_mac) && mac_addrs_match(&(rhost->server_mac), &(context.active_host->server_mac))) {
          printf("FOUND MATCH\n");
          context.config.registered_hosts[rhost_idx] = context.active_host;
          break;
        }
      }
    } else {
      context.active_host->registered_state = event->registered_host;
      memcpy(&context.active_host->server_mac, &(event->registered_host->server_mac), 6);
      context.config.registered_hosts[context.config.num_registered_hosts++] = context.active_host;
    }

    config_serialize(&context.config);
  }

  chiaki_regist_stop(&regist);
	chiaki_regist_fini(&regist);
}

int host_register(VitaChiakiHost* host, int pin) {
  if (!host->hostname || !host->discovery_state) {
    return 1;
  }
  ChiakiRegistInfo regist_info = {};
  regist_info.target = host->target;
  size_t account_id_size = sizeof(uint8_t[CHIAKI_PSN_ACCOUNT_ID_SIZE]);
  chiaki_base64_decode(context.config.psn_account_id, /*sizeof(context.config.psn_account_id)*/12, regist_info.psn_account_id, &(account_id_size));
  regist_info.psn_online_id = NULL;
	regist_info.pin = pin;
  regist_info.host = host->hostname;
  regist_info.broadcast = false;
	chiaki_regist_start(&regist, &context.log, &regist_info, regist_cb, NULL);
  return 0;
}

int host_wakeup(VitaChiakiHost* host) {
  if (!host->hostname) {
    LOGE("Missing hostname. Cannot send wakeup signal.");
    return 1;
  }
  LOGD("Attempting to send wakeup signal....");
	uint64_t credential = (uint64_t)strtoull(host->registered_state->rp_regist_key, NULL, 16);
  chiaki_discovery_wakeup(&context.log,
                          context.discovery_enabled ? &context.discovery.discovery : NULL,
                          host->hostname, credential,
                          chiaki_target_is_ps5(host->target));
  return 0;
}

static void event_cb(ChiakiEvent *event, void *user) {
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			LOGD("EventCB CHIAKI_EVENT_CONNECTED");
      context.stream.inputs_ready = true;
      context.stream.next_stream_allowed_us = 0;
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			LOGD("EventCB CHIAKI_EVENT_LOGIN_PIN_REQUEST");
			break;
		case CHIAKI_EVENT_RUMBLE:
			LOGD("EventCB CHIAKI_EVENT_RUMBLE");
			break;
		case CHIAKI_EVENT_QUIT:
			LOGE("EventCB CHIAKI_EVENT_QUIT");
	    chiaki_opus_decoder_fini(&context.stream.opus_decoder);
      vita_h264_cleanup();
      vita_audio_cleanup();
      context.stream.is_streaming = false;
      context.stream.inputs_ready = false;
      ui_clear_waking_wait();
      context.stream.session_init = false;
      uint64_t now_us = sceKernelGetProcessTimeWide();
      if (context.stream.stop_requested) {
        context.stream.next_stream_allowed_us = 0;
      } else {
        context.stream.next_stream_allowed_us = now_us + STREAM_RETRY_COOLDOWN_US;
      }
      context.stream.stop_requested = false;
      resume_discovery_if_needed();
      reset_stream_metrics();
			break;
	}
}

static void reset_stream_metrics(void) {
  context.stream.measured_bitrate_mbps = 0.0f;
  context.stream.measured_rtt_ms = 0;
  context.stream.metrics_last_update_us = 0;
}

static void update_latency_metrics(void) {
  if (!context.stream.session_init)
    return;

  ChiakiStreamConnection *stream_connection = &context.stream.session.stream_connection;
  ChiakiVideoReceiver *receiver = stream_connection->video_receiver;
  if (!receiver)
    return;

  uint32_t fps = context.stream.session.connect_info.video_profile.max_fps;
  if (fps == 0)
    fps = 30;

  ChiakiStreamStats *stats = &receiver->frame_processor.stream_stats;
  uint64_t bitrate_bps = chiaki_stream_stats_bitrate(stats, fps);
  float bitrate_mbps = bitrate_bps > 0 ? ((float)bitrate_bps / 1000000.0f) : 0.0f;
  uint32_t rtt_ms = (uint32_t)(context.stream.session.rtt_us / 1000);
  uint64_t now_us = sceKernelGetProcessTimeWide();

  context.stream.measured_bitrate_mbps = bitrate_mbps;
  context.stream.measured_rtt_ms = rtt_ms;
  context.stream.metrics_last_update_us = now_us;

  if (!context.config.show_latency)
    return;

  static const uint64_t LOG_INTERVAL_US = 1000000;
  static uint64_t last_log_us = 0;
  if (now_us - last_log_us >= LOG_INTERVAL_US) {
    float target_mbps = context.stream.session.connect_info.video_profile.bitrate / 1000.0f;
    LOGD("Latency metrics — target %.2f Mbps, measured %.2f Mbps, RTT %u ms",
         target_mbps, bitrate_mbps, rtt_ms);
    last_log_us = now_us;
  }
}

static unsigned int latency_mode_target_kbps(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return 1200;
    case VITA_LATENCY_MODE_LOW: return 1800;
    case VITA_LATENCY_MODE_HIGH: return 3200;
    case VITA_LATENCY_MODE_MAX: return 3800;
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return 2600;
  }
}

static void apply_latency_mode(ChiakiConnectVideoProfile *profile, VitaChiakiLatencyMode mode) {
  unsigned int target = latency_mode_target_kbps(mode);
  profile->bitrate = target;
  LOGD("Latency mode applied: %u kbps @ %ux%u", target, profile->width, profile->height);
}

static void request_stream_stop(const char *reason) {
  if (!context.stream.session_init)
    return;
  if (!context.stream.stop_requested) {
    LOGD("Stopping stream (%s)", reason ? reason : "user");
    context.stream.stop_requested = true;
  }
  context.stream.next_stream_allowed_us = 0;
  chiaki_session_stop(&context.stream.session);
}

static void resume_discovery_if_needed(void) {
  if (context.discovery_resume_after_stream) {
    LOGD("Resuming discovery after stream");
    start_discovery(NULL, NULL);
    context.discovery_resume_after_stream = false;
  }
}

static bool video_cb(uint8_t *buf, size_t buf_size, void *user) {
  static bool first_frame = true;
  if (context.stream.stop_requested)
    return false;
  if (first_frame) {
    LOGD("VIDEO CALLBACK: First frame received (size=%zu)", buf_size);
    first_frame = false;
  }
  context.stream.is_streaming = true;
  int err = vita_h264_decode_frame(buf, buf_size);
  if (err != 0) {
		LOGE("Error during video decode: %d", err);
    return false;
  }
  update_latency_metrics();
  return true;
}

static void set_ctrl_out(VitaChiakiStream *stream, VitakiCtrlOut ctrl_out) {
  if (ctrl_out == VITAKI_CTRL_OUT_L2) {
    stream->controller_state.l2_state = 0xff;
  } else if (ctrl_out == VITAKI_CTRL_OUT_R2) {
    stream->controller_state.r2_state = 0xff;
  } else {
    // in this case ctrl_out is a controller button mask
    //if (ctrl_out == VITAKI_CTRL_OUT_NONE) {
    //// do nothing
    //} else {
    stream->controller_state.buttons |= ctrl_out;
    //}
  }
}

void set_ctrl_l2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in) {
  // check if ctrl_in should be l2; if not, hit corresponding mapped button
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_l2 == ctrl_in) {
    stream->controller_state.l2_state = 0xff;
  } else {
    stream->controller_state.buttons |= vcmi.in_out_btn[ctrl_in];
  }
}
void set_ctrl_r2pos(VitaChiakiStream *stream, VitakiCtrlIn ctrl_in) {
  // check if ctrl_in should be r2; if not, hit corresponding mapped button
  VitakiCtrlMapInfo vcmi = stream->vcmi;
  if (vcmi.in_r2 == ctrl_in) {
    stream->controller_state.r2_state = 0xff;
  } else {
    stream->controller_state.buttons |= vcmi.in_out_btn[ctrl_in];
  }
}

static void *input_thread_func(void* user) {
  // Set input thread to highest priority for lowest input lag
  // Pin to CPU1 to avoid contention with video/audio threads on CPU0
  // Priority 96 is higher than video (64) so input takes precedence
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 96);
  sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_1);

  sceMotionStartSampling();
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  SceCtrlData ctrl;
  SceMotionState motion;
	VitaChiakiStream *stream = user;
  int ms_per_loop = 2;

  VitakiCtrlMapInfo vcmi = stream->vcmi;

  if (!vcmi.did_init) init_controller_map(&vcmi, context.config.controller_map_id);

  // Touchscreen setup
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);
	sceTouchEnableTouchForce(SCE_TOUCH_PORT_BACK);
	SceTouchData touch[SCE_TOUCH_PORT_MAX_NUM];
  int TOUCH_MAX_WIDTH = 1919;
  int TOUCH_MAX_HEIGHT = 1087;
  int TOUCH_MAX_WIDTH_BY_2 = TOUCH_MAX_WIDTH/2;
  int TOUCH_MAX_HEIGHT_BY_2 = TOUCH_MAX_HEIGHT/2;
  int TOUCH_MAX_WIDTH_BY_4 = TOUCH_MAX_WIDTH/4;
  int TOUCH_MAX_HEIGHT_BY_4 = TOUCH_MAX_HEIGHT/4;
  int FRONT_ARC_RADIUS = TOUCH_MAX_HEIGHT/3;
  int FRONT_ARC_RADIUS_2 = FRONT_ARC_RADIUS*FRONT_ARC_RADIUS;
  // note: rear touch may be active in Y only from 108 to 889? (see Vita3K code)

  // manually create bools for each combo (since there's only 5)
  bool vitaki_reartouch_left_l1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_LEFT_L1] != 0) || (vcmi.in_l2 == VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
  bool vitaki_reartouch_right_r1_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1] != 0) || (vcmi.in_r2 == VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
  bool vitaki_select_start_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START] != 0);
  bool vitaki_left_square_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_LEFT_SQUARE] != 0) || (vcmi.in_l2 == VITAKI_CTRL_IN_LEFT_SQUARE);
  bool vitaki_right_circle_mapped = (vcmi.in_out_btn[VITAKI_CTRL_IN_RIGHT_CIRCLE] != 0) || (vcmi.in_r2 == VITAKI_CTRL_IN_RIGHT_CIRCLE);

  static int wait_count = 0;
  static int exit_combo_hold = 0;
  const int EXIT_COMBO_THRESHOLD = 500;  // ~1s with 2ms loop
  while (true) {

    // TODO enable using triggers as L2, R2
    // TODO enable home button, with long hold sent back to Vita?


    if (stream->inputs_ready) {
      if (wait_count > 0) {
        LOGD("INPUT THREAD: inputs_ready after %d loops", wait_count);
        wait_count = 0;
      }
      int start_time_us = sceKernelGetProcessTimeWide();

      // get button state
      sceCtrlPeekBufferPositive(0, &ctrl, 1);

      bool exit_combo = (ctrl.buttons & SCE_CTRL_LTRIGGER) &&
                        (ctrl.buttons & SCE_CTRL_RTRIGGER) &&
                        (ctrl.buttons & SCE_CTRL_START);
      if (exit_combo && stream->session_init && !stream->stop_requested) {
        exit_combo_hold++;
        if (exit_combo_hold >= EXIT_COMBO_THRESHOLD) {
          request_stream_stop("L+R+Start");
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

      // get touchscreen state
      for(int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
        sceTouchPeek(port, &touch[port], 1);
      }

      // get gyro/accel state
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

      // 0-255 conversion
      stream->controller_state.left_x = (ctrl.lx - 128) * 2 * 0x7F/*.FF*/;
      stream->controller_state.left_y = (ctrl.ly - 128) * 2 * 0x7F/*.FF*/;
      stream->controller_state.right_x = (ctrl.rx - 128) * 2 * 0x7F/*.FF*/;
      stream->controller_state.right_y = (ctrl.ry - 128) * 2 * 0x7F/*.FF*/;

      stream->controller_state.buttons = 0x00;
      stream->controller_state.l2_state = 0x00;
      stream->controller_state.r2_state = 0x00;

      bool reartouch_right = false;
      bool reartouch_left = false;

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_BACK].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_BACK].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_BACK].report[touch_i].y;

        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_REARTOUCH_ANY];

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_RIGHT);
          reartouch_right = true;
          if (y > TOUCH_MAX_HEIGHT_BY_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LR);
          } else {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_UR);
          }
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LEFT);
          reartouch_left = true;
          if (y > TOUCH_MAX_HEIGHT_BY_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LL);
          } else {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_UL);
          }
        }
      }

      for (int touch_i = 0; touch_i < touch[SCE_TOUCH_PORT_FRONT].reportNum; touch_i++) {
        int x = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].x;
        int y = touch[SCE_TOUCH_PORT_FRONT].report[touch_i].y;
        stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_ANY];

        if (x > TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_RIGHT);

          if (y*y + (x-TOUCH_MAX_WIDTH)*(x-TOUCH_MAX_WIDTH) <= FRONT_ARC_RADIUS_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_UR_ARC);
          } else if ((y-TOUCH_MAX_HEIGHT)*(y-TOUCH_MAX_HEIGHT) + (x-TOUCH_MAX_WIDTH)*(x-TOUCH_MAX_WIDTH) <= FRONT_ARC_RADIUS_2) {
            set_ctrl_r2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LR_ARC);
          }
        } else if (x < TOUCH_MAX_WIDTH_BY_2) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LEFT);

          if (y*y + x*x <= FRONT_ARC_RADIUS_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_UL_ARC);
          } else if ((y-TOUCH_MAX_HEIGHT)*(y-TOUCH_MAX_HEIGHT) + x*x <= FRONT_ARC_RADIUS_2) {
            set_ctrl_l2pos(stream, VITAKI_CTRL_IN_FRONTTOUCH_LL_ARC);
          }
        }

        if ((x >= TOUCH_MAX_WIDTH_BY_4) && (x <= TOUCH_MAX_WIDTH - TOUCH_MAX_WIDTH_BY_4)
            && (y >= TOUCH_MAX_HEIGHT_BY_4) && (y <= TOUCH_MAX_HEIGHT - TOUCH_MAX_HEIGHT_BY_4)
            ) {
          stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_FRONTTOUCH_CENTER];
        }
      }

      // cursed conversion
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
      // what is L3??
      if (ctrl.buttons & SCE_CTRL_L3)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
      if (ctrl.buttons & SCE_CTRL_R3)       stream->controller_state.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;

      if (ctrl.buttons & SCE_CTRL_LTRIGGER) {
        if (reartouch_left && vitaki_reartouch_left_l1_mapped) {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_REARTOUCH_LEFT_L1);
        } else {
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_L1);
        }
      }
      if (ctrl.buttons & SCE_CTRL_RTRIGGER) {
        if (reartouch_right && vitaki_reartouch_right_r1_mapped) {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_REARTOUCH_RIGHT_R1);
        } else {
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_R1);
        }
      }

      // Select + Start
      if (vitaki_select_start_mapped) {
        if ((ctrl.buttons & SCE_CTRL_SELECT) && (ctrl.buttons & SCE_CTRL_START)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_SHARE;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_OPTIONS;
          stream->controller_state.buttons |= vcmi.in_out_btn[VITAKI_CTRL_IN_SELECT_START];
        }
      }

      // Dpad-left + Square
      if (vitaki_left_square_mapped) {
        if ((ctrl.buttons & SCE_CTRL_LEFT) && (ctrl.buttons & SCE_CTRL_SQUARE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_BOX;
          set_ctrl_l2pos(stream, VITAKI_CTRL_IN_LEFT_SQUARE);
        }
      }
      // Dpad-right + Circle
      if (vitaki_right_circle_mapped) {
        if ((ctrl.buttons & SCE_CTRL_RIGHT) && (ctrl.buttons & SCE_CTRL_CIRCLE)) {
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
          stream->controller_state.buttons &= ~CHIAKI_CONTROLLER_BUTTON_MOON;
          set_ctrl_r2pos(stream, VITAKI_CTRL_IN_RIGHT_CIRCLE);
        }
      }

      chiaki_session_set_controller_state(&stream->session, &stream->controller_state);
      // LOGD("ly 0x%x %d", ctrl.ly, ctrl.ly);

      // Adjust sleep time to account for calculations above
      int diff_time_us = sceKernelGetProcessTimeWide() - start_time_us;
      if (diff_time_us >= ms_per_loop*1000) {
        // Control loop appears to usually take ~70-90 us, sometimes up to 130
        // Far less than the 5000 us of the default loop
        LOGD("SLOW CTRL LOOP! %d microseconds", diff_time_us);
      } else {
        usleep(ms_per_loop*1000 - diff_time_us);
      }

    } else {
      // Not streaming yet - count wait loops
      wait_count++;
      if (wait_count == 1000) {
        LOGD("INPUT THREAD: Waiting for inputs_ready (1000 loops)");
      }
      usleep(1000);  // Sleep 1ms to avoid tight spin
    }
  }

  return 0;
}

int host_stream(VitaChiakiHost* host) {
  LOGD("Preparing to start host_stream");
  if (!host->hostname || !host->registered_state) {
    return 1;
  }
  if (context.stream.session_init) {
    LOGD("Stream already initialized; ignoring duplicate start request");
    return 1;
  }
  if (context.discovery_enabled) {
    LOGD("Suspending discovery during stream");
    stop_discovery(true);
    context.discovery_resume_after_stream = true;
  } else {
    context.discovery_resume_after_stream = false;
  }

  int result = 1;
  context.stream.stop_requested = false;
  context.stream.inputs_ready = false;
  context.stream.is_streaming = false;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.next_stream_allowed_us &&
      now_us < context.stream.next_stream_allowed_us) {
    uint64_t remaining_ms =
        (context.stream.next_stream_allowed_us - now_us + 999) / 1000;
    LOGD("Stream start blocked for %llu ms to let console recover", remaining_ms);
    goto cleanup;
  }

  ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile,
		context.config.resolution, context.config.fps);
  apply_latency_mode(&profile, context.config.latency_mode);

	ChiakiConnectInfo chiaki_connect_info = {};
	chiaki_connect_info.host = host->hostname;
	chiaki_connect_info.video_profile = profile;
	chiaki_connect_info.video_profile_auto_downgrade = true;
	chiaki_connect_info.ps5 = chiaki_target_is_ps5(host->target);
	memcpy(chiaki_connect_info.regist_key, host->registered_state->rp_regist_key, sizeof(chiaki_connect_info.regist_key));
	memcpy(chiaki_connect_info.morning, host->registered_state->rp_key, sizeof(chiaki_connect_info.morning));

	ChiakiErrorCode err = chiaki_session_init(&context.stream.session, &chiaki_connect_info, &context.log);
	if(err != CHIAKI_ERR_SUCCESS) {
		LOGE("Error during stream setup: %s", chiaki_error_string(err));
    goto cleanup;
  }
	init_controller_map(&(context.stream.vcmi), context.config.controller_map_id);
	context.stream.session_init = true;
  reset_stream_metrics();
	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_init(&context.stream.opus_decoder, &context.log);
	chiaki_opus_decoder_set_cb(&context.stream.opus_decoder, vita_audio_init, vita_audio_cb, NULL);
	chiaki_opus_decoder_get_sink(&context.stream.opus_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&context.stream.session, &audio_sink);
  chiaki_session_set_video_sample_cb(&context.stream.session, video_cb, NULL);
	chiaki_session_set_event_cb(&context.stream.session, event_cb, NULL);
	chiaki_controller_state_set_idle(&context.stream.controller_state);

  err = vita_h264_setup(profile.width, profile.height);
  if (err != 0) {
		LOGE("Error during video start: %d", err);
    goto cleanup;
  }
  vita_h264_start();

	err = chiaki_session_start(&context.stream.session);
  if(err != CHIAKI_ERR_SUCCESS) {
		LOGE("Error during stream start: %s", chiaki_error_string(err));
    goto cleanup;
  }

	err = chiaki_thread_create(&context.stream.input_thread, input_thread_func, &context.stream);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		LOGE("Failed to create input thread");
	}

  result = 0;

cleanup:
  if (result != 0)
    resume_discovery_if_needed();
  return result;
}

/// Check if two MAC addresses match
bool mac_addrs_match(MacAddr* a, MacAddr* b) {
  for (int j = 0; j < 6; j++) {
    if ((*a)[j] != (*b)[j]) {
      return false;
    }
  }
  return true;
}

/// Save a new manual host into the context, given existing registered host and new remote ip ("hostname")
void save_manual_host(VitaChiakiHost* rhost, char* new_hostname) {
  if ((!rhost->server_mac)) {
    CHIAKI_LOGE(&(context.log), "Failed to get registered host mac; could not save.");
  }

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (mac_addrs_match(&(h->server_mac), &(rhost->server_mac))) {
      if (strcmp(h->hostname, new_hostname) == 0) {
        // this manual host already exists (same mac addr and hostname)
        CHIAKI_LOGW(&(context.log), "Duplicate manual host. Not saving.");
        return;
      }
    }
  }

  VitaChiakiHost* newhost = (VitaChiakiHost*)malloc(sizeof(VitaChiakiHost));
  copy_host(newhost, rhost, false);
  newhost->hostname = strdup(new_hostname);
  newhost->type = REGISTERED | MANUALLY_ADDED;

  CHIAKI_LOGI(&(context.log), "--");
  CHIAKI_LOGI(&(context.log), "Adding manual host:");

  if(newhost->hostname)
    CHIAKI_LOGI(&(context.log), "Host Name (address):               %s", newhost->hostname);
  if(newhost->server_mac) {
    CHIAKI_LOGI(&(context.log), "Host MAC:                          %X%X%X%X%X%X\n", newhost->server_mac[0], newhost->server_mac[1], newhost->server_mac[2], newhost->server_mac[3], newhost->server_mac[4], newhost->server_mac[5]);
  }
  CHIAKI_LOGI(&(context.log),   "Is PS5:                            %s", chiaki_target_is_ps5(newhost->target) ? "true" : "false");

  CHIAKI_LOGI(&(context.log), "--");

  if (context.config.num_manual_hosts >= MAX_NUM_HOSTS) { // TODO change to manual max
    CHIAKI_LOGE(&(context.log), "Max manual hosts reached; could not save.");
    return;
  }

  // Save to manual hosts
  context.config.manual_hosts[context.config.num_manual_hosts++] = newhost;

  // Save config
  config_serialize(&context.config);

  LOGD("> UPDATE CONTEXT...");
  // update hosts in context
  update_context_hosts();
  LOGD("> UPDATE CONTEXT DONE");
}


void delete_manual_host(VitaChiakiHost* mhost) {

  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (h == mhost) { // same object
      context.config.manual_hosts[i] = NULL;
    }
  }
  host_free(mhost);

  // reorder manual hosts
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* h = context.config.manual_hosts[i];
    if (!h) {
      for (int j = i+1; j < context.config.num_manual_hosts; j++) {
        context.config.manual_hosts[j-1] = context.config.manual_hosts[j];
      }
      context.config.manual_hosts[context.config.num_manual_hosts-1] = NULL;
      context.config.num_manual_hosts--;
    }
  }

  // Save config
  config_serialize(&context.config);

  // update hosts in context
  update_context_hosts();

}

int count_nonnull_context_hosts() {
  int sum = 0;
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost *h = context.hosts[host_idx];
    if (h) {
      sum += 1;
    }
  }
  return sum;
}

void update_context_hosts() {
  bool hide_remote_if_discovered = true;

  // Remove any no-longer-existent manual hosts
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
    VitaChiakiHost* h = context.hosts[host_idx];
    if (h && (h->type & MANUALLY_ADDED)) {

      // check if this host still exists
      bool host_exists = false;
      for (int i = 0; i < context.config.num_manual_hosts; i++) {
        if (context.config.manual_hosts[i] == h) {
          host_exists = true;
          break;
        }
      }
      if (!host_exists) {
        context.hosts[host_idx] = NULL;
      }
    }
  }

  // Remove any manual hosts matching discovered hosts
  if (hide_remote_if_discovered) {
    for (int i = 0; i < MAX_NUM_HOSTS; i++) {
      VitaChiakiHost* mhost = context.hosts[i];
      if (!(mhost && mhost->server_mac && (mhost->type & MANUALLY_ADDED))) continue;
      for (int j = 0; j < MAX_NUM_HOSTS; j++) {
        if (j == i) continue;
        VitaChiakiHost* h = context.hosts[j];
        if (!(h && h->server_mac && (h->type & DISCOVERED) && !(h->type & MANUALLY_ADDED))) continue;
        if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
          context.hosts[i] = NULL;
        }
      }
    }
  }


  // Remove any empty slots
  for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (!h) {
        // slide all hosts back one slot
        for (int j = host_idx+1; j < MAX_NUM_HOSTS; j++) {
          context.hosts[j-1] = context.hosts[j];
        }
        context.hosts[MAX_NUM_HOSTS-1] = NULL;
      }
  }

  // Add in manual hosts
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* mhost = context.config.manual_hosts[i];

    // first, check if it (or the local discovered version of the same console) is already in context
    bool already_in_context = false;
    for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (!h) continue;
      if ((!h->server_mac) || (!h->hostname)) continue;
      if (mac_addrs_match(&(h->server_mac), &(mhost->server_mac))) {
        // it's the same console

        if ((h->type & DISCOVERED) && hide_remote_if_discovered) {
          // found matching discovered console
          already_in_context = true;
          break;
        }

        if ((h->type & MANUALLY_ADDED) && (strcmp(h->hostname, mhost->hostname) == 0)) {
          // found identical manual console
          already_in_context = true;
          break;
        }
      }
    }

    if (already_in_context) {
      continue;
    }

    // the host is not in the context yet. Find an empty spot for it, if possible.
    bool added_to_context = false;
    for (int host_idx = 0; host_idx < MAX_NUM_HOSTS; host_idx++) {
      VitaChiakiHost* h = context.hosts[host_idx];
      if (h == NULL) {
        // empty spot
        context.hosts[host_idx] = mhost;
        added_to_context = true;
        break;
      }
    }

    if (!added_to_context) {
      CHIAKI_LOGE(&(context.log), "Max # of hosts reached; could not add manual host %d to context.", i);
    }

  }

  // Update num_hosts
  context.num_hosts = count_nonnull_context_hosts();
}

int count_manual_hosts_of_console(VitaChiakiHost* host) {
  if (!host) return 0;
  if (!host->server_mac) return 0;
  int sum = 0;
  for (int i = 0; i < context.config.num_manual_hosts; i++) {
    VitaChiakiHost* mhost = context.config.manual_hosts[i];
    if (!mhost) continue;
    if (!mhost->server_mac) continue;
    /*LOGD("CHECKING %X%X%X%X%X%X vs %X%X%X%X%X%X",
         host->server_mac[0], host->server_mac[1], host->server_mac[2], host->server_mac[3], host->server_mac[4], host->server_mac[5],
         mhost->server_mac[0], mhost->server_mac[1], mhost->server_mac[2], mhost->server_mac[3], mhost->server_mac[4], mhost->server_mac[5]
         );*/
    if (mac_addrs_match(&(host->server_mac), &(mhost->server_mac))) {
      sum++;
    }
  }
  return sum;
}

void copy_host(VitaChiakiHost* h_dest, VitaChiakiHost* h_src, bool copy_hostname) {
        h_dest->type = h_src->type;
        h_dest->target = h_src->target;
        if (h_src->server_mac) {
          memcpy(&h_dest->server_mac, &(h_src->server_mac), 6);
        }

        h_dest->hostname = NULL;
        if ((h_src->hostname) && copy_hostname) {
          h_dest->hostname = strdup(h_src->hostname);
        }

        // copy registered state
        h_dest->registered_state = NULL;
        ChiakiRegisteredHost* rstate_src = h_src->registered_state;
        if (rstate_src) {
          ChiakiRegisteredHost* rstate_dest = malloc(sizeof(ChiakiRegisteredHost));
          h_dest->registered_state = rstate_dest;
          copy_host_registered_state(rstate_dest, rstate_src);
        }

        // don't copy discovery state
        h_dest->discovery_state = NULL;
}

void copy_host_registered_state(ChiakiRegisteredHost* rstate_dest, ChiakiRegisteredHost* rstate_src) {
  if (rstate_src) {
    if (rstate_src->server_nickname) {
      strncpy(rstate_dest->server_nickname, rstate_src->server_nickname, sizeof(rstate_dest->server_nickname));
    }
    rstate_dest->target = rstate_src->target;
    memcpy(rstate_dest->rp_key, rstate_src->rp_key, sizeof(rstate_dest->rp_key));
    rstate_dest->rp_key_type = rstate_src->rp_key_type;
    memcpy(rstate_dest->rp_regist_key, rstate_src->rp_regist_key, sizeof(rstate_dest->rp_regist_key));
  }
}
