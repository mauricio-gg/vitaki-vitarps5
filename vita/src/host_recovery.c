#include "context.h"
#include "host_constants.h"
#include "host_recovery.h"
#include "host_feedback.h"

#include <psp2/kernel/processmgr.h>
#include <string.h>

#define LOSS_RECOVERY_ACTION_COOLDOWN_US (10 * 1000 * 1000ULL)
#define RESTART_FAILURE_COOLDOWN_US (5000 * 1000ULL)
#define FAST_RESTART_RETRY_DELAY_US (250 * 1000ULL)
#define FAST_RESTART_MAX_ATTEMPTS 2
#define RECONNECT_RECOVER_LOW_FPS_TRIGGER_WINDOWS 12
#define MAX_AUTO_RECONNECT_ATTEMPTS 3
#define RECONNECT_RECOVER_ACTION_COOLDOWN_US (2 * 1000 * 1000ULL)
#define RECONNECT_RECOVER_STAGE2_WAIT_US (8 * 1000 * 1000ULL)
#define RECONNECT_RECOVER_TARGET_KBPS 900
#define RECONNECT_RECOVER_MIN_HEALTHY_FPS 27
#define FAST_RESTART_BITRATE_CAP_KBPS 1500

#define RECONNECT_RECOVER_STAGE_IDLE 0
#define RECONNECT_RECOVER_STAGE_IDR_REQUESTED 1
#define RECONNECT_RECOVER_STAGE_SOFT_RESTARTED 2
#define RECONNECT_RECOVER_STAGE_ESCALATED 3

static const char *restart_source_label(const char *source) {
  return (source && source[0]) ? source : "unknown";
}

static bool request_stream_restart(uint32_t bitrate_kbps) {
  if (!context.stream.session_init) {
    LOGE("Cannot restart stream — session not initialized");
    return false;
  }
  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.last_restart_failure_us &&
      now_us - context.stream.last_restart_failure_us < RESTART_FAILURE_COOLDOWN_US) {
    uint64_t remaining = RESTART_FAILURE_COOLDOWN_US -
        (now_us - context.stream.last_restart_failure_us);
    LOGD("Restart cooldown active — delaying %llu ms", remaining / 1000ULL);
    return false;
  }
  if (context.stream.fast_restart_active) {
    LOGD("Soft restart already active; ignoring duplicate request");
    return true;
  }

  ChiakiConnectVideoProfile profile = context.stream.session.connect_info.video_profile;
  if (bitrate_kbps > 0) {
    profile.bitrate = bitrate_kbps;
  } else {
    profile.bitrate = LOSS_RETRY_BITRATE_KBPS;
  }

  if (context.config.clamp_soft_restart_bitrate &&
      profile.bitrate > FAST_RESTART_BITRATE_CAP_KBPS) {
    LOGD("Soft restart bitrate %u kbps exceeds cap %u kbps — clamping",
         profile.bitrate, FAST_RESTART_BITRATE_CAP_KBPS);
    profile.bitrate = FAST_RESTART_BITRATE_CAP_KBPS;
  }

  ChiakiErrorCode err = CHIAKI_ERR_UNKNOWN;
  for (uint32_t attempt = 0; attempt < FAST_RESTART_MAX_ATTEMPTS; ++attempt) {
    err = chiaki_session_request_stream_restart(&context.stream.session, &profile);
    if (err == CHIAKI_ERR_SUCCESS) {
      if (attempt > 0) {
        LOGD("Soft restart request succeeded on retry %u", attempt + 1);
      }
      break;
    }
    LOGE("Soft restart request attempt %u failed: %s",
         attempt + 1,
         chiaki_error_string(err));
    if (attempt + 1 < FAST_RESTART_MAX_ATTEMPTS) {
      sceKernelDelayThread(FAST_RESTART_RETRY_DELAY_US);
    }
  }
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Failed to request soft stream restart after %u attempt(s)",
         FAST_RESTART_MAX_ATTEMPTS);
    return false;
  }

  context.stream.fast_restart_active = true;
  context.stream.is_streaming = false;
  context.stream.reconnect_overlay_active = true;
  context.stream.reconnect_overlay_start_us = sceKernelGetProcessTimeWide();
  context.stream.inputs_ready = true;
  context.stream.inputs_resume_pending = true;
  context.stream.restart_failure_active = false;
  return true;
}

static bool request_stream_restart_coordinated(const char *source,
                                               uint32_t bitrate_kbps,
                                               uint64_t now_us) {
  const char *source_label = restart_source_label(source);
  if (context.stream.stop_requested) {
    LOGD("PIPE/RESTART source=%s action=skip reason=stop_requested", source_label);
    return false;
  }
  if (context.stream.auto_reconnect_count >= MAX_AUTO_RECONNECT_ATTEMPTS) {
    LOGD("PIPE/RESTART source=%s action=suppressed_max_reconnects auto_count=%u gen=%u",
         source_label, context.stream.auto_reconnect_count, context.stream.reconnect_generation);
    return false;
  }
  if (context.stream.fast_restart_active) {
    LOGD("PIPE/RESTART source=%s action=skip reason=restart_active", source_label);
    return true;
  }
  if (context.stream.restart_cooloff_until_us &&
      now_us < context.stream.restart_cooloff_until_us) {
    uint64_t remaining_ms =
        (context.stream.restart_cooloff_until_us - now_us) / 1000ULL;
    LOGD("PIPE/RESTART source=%s action=blocked_cooloff remaining=%llums",
         source_label, (unsigned long long)remaining_ms);
    return false;
  }

  if (context.stream.last_loss_recovery_action_us &&
      now_us - context.stream.last_loss_recovery_action_us <
          LOSS_RECOVERY_ACTION_COOLDOWN_US) {
    uint64_t remaining_ms =
        (LOSS_RECOVERY_ACTION_COOLDOWN_US -
         (now_us - context.stream.last_loss_recovery_action_us)) / 1000ULL;
    LOGD("PIPE/RESTART source=%s action=cooldown_skip remaining=%llums",
         source_label, (unsigned long long)remaining_ms);
    return false;
  }

  if (strcmp(context.stream.last_restart_source, source_label) != 0) {
    sceClibSnprintf(context.stream.last_restart_source,
                    sizeof(context.stream.last_restart_source),
                    "%s",
                    source_label);
    context.stream.restart_source_attempts = 1;
  } else if (context.stream.restart_source_attempts < UINT32_MAX) {
    context.stream.restart_source_attempts++;
  }

  bool ok = request_stream_restart(bitrate_kbps);
  if (ok) {
    context.stream.auto_reconnect_count++;
    context.stream.last_loss_recovery_action_us = now_us;
    LOGD("PIPE/RESTART source=%s action=requested bitrate=%u attempt=%u auto_count=%u",
         source_label,
         bitrate_kbps,
         context.stream.restart_source_attempts,
         context.stream.auto_reconnect_count);
  } else {
    LOGE("PIPE/RESTART source=%s action=failed bitrate=%u attempt=%u",
         source_label,
         bitrate_kbps,
         context.stream.restart_source_attempts);
  }
  return ok;
}

static void reset_reconnect_recovery_state(void) {
  context.stream.reconnect.recover_active = false;
  context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_IDLE;
  context.stream.reconnect.recover_last_action_us = 0;
  context.stream.reconnect.recover_idr_attempts = 0;
  context.stream.reconnect.recover_restart_attempts = 0;
  context.stream.reconnect.recover_stable_windows = 0;
}

static void start_reconnect_recovery_state(void) {
  context.stream.reconnect.recover_active = true;
  context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_IDLE;
  context.stream.reconnect.recover_idr_attempts = 0;
  context.stream.reconnect.recover_restart_attempts = 0;
  context.stream.reconnect.recover_stable_windows = 0;
}

void host_recovery_handle_post_reconnect_degraded_mode(bool av_diag_progressed,
                                                       uint32_t incoming_fps,
                                                       uint32_t target_fps,
                                                       bool low_fps_window,
                                                       uint64_t now_us) {
  if (context.stream.stop_requested || context.stream.fast_restart_active)
    return;

  bool reconnect_window_active = context.stream.post_reconnect_window_until_us &&
      now_us <= context.stream.post_reconnect_window_until_us;
  if (!reconnect_window_active)
    return;

  bool degraded =
      context.stream.post_reconnect_low_fps_windows >=
          RECONNECT_RECOVER_LOW_FPS_TRIGGER_WINDOWS &&
      av_diag_progressed;

  bool healthy_window = target_fps > 0 &&
      incoming_fps >= RECONNECT_RECOVER_MIN_HEALTHY_FPS &&
      !av_diag_progressed;
  if (context.stream.reconnect.recover_active) {
    if (healthy_window) {
      context.stream.reconnect.recover_stable_windows++;
      if (context.stream.reconnect.recover_stable_windows >= 2) {
        LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stabilized stage=%u fps=%u/%u",
             context.stream.session_generation,
             context.stream.reconnect_generation,
             context.stream.reconnect.recover_stage,
             incoming_fps,
             target_fps);
        reset_reconnect_recovery_state();
      }
    } else if (low_fps_window || av_diag_progressed) {
      context.stream.reconnect.recover_stable_windows = 0;
    }
  }

  if (!degraded)
    return;

  if (context.stream.reconnect.recover_last_action_us &&
      now_us - context.stream.reconnect.recover_last_action_us <
          RECONNECT_RECOVER_ACTION_COOLDOWN_US) {
    return;
  }

  if (!context.stream.reconnect.recover_active) {
    start_reconnect_recovery_state();
    LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=trigger low_windows=%u fps=%u/%u",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         context.stream.post_reconnect_low_fps_windows,
         incoming_fps,
         target_fps);
  }

  if (context.stream.reconnect.recover_stage == RECONNECT_RECOVER_STAGE_IDLE) {
    host_request_decoder_resync("post-reconnect degraded stage1");
    context.stream.reconnect.recover_idr_attempts++;
    context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_IDR_REQUESTED;
    context.stream.reconnect.recover_last_action_us = now_us;
    if (context.active_host) {
      host_set_hint(context.active_host,
                    "Video references unstable - requesting keyframe",
                    false,
                    HINT_DURATION_KEYFRAME_US);
    }
    LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage1_idr idr_attempts=%u fps=%u/%u",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         context.stream.reconnect.recover_idr_attempts,
         incoming_fps,
         target_fps);
    return;
  }

  if (context.stream.reconnect.recover_stage == RECONNECT_RECOVER_STAGE_IDR_REQUESTED) {
    bool stage2_av_distress = av_diag_progressed;
    bool restart_cooloff_active = context.stream.restart_cooloff_until_us &&
        now_us < context.stream.restart_cooloff_until_us;
    bool stage2_source_backoff = strcmp(context.stream.last_restart_source,
                                        "post_reconnect_stage2") == 0 &&
        context.stream.restart_source_attempts > 1 &&
        context.stream.last_restart_handshake_fail_us &&
        now_us - context.stream.last_restart_handshake_fail_us <=
            RESTART_HANDSHAKE_REPEAT_WINDOW_US;
    if (!stage2_av_distress || restart_cooloff_active || stage2_source_backoff) {
      const char *reason = !stage2_av_distress ? "no_av_distress"
          : restart_cooloff_active ? "restart_cooloff"
          : "source_backoff";
      host_request_decoder_resync("post-reconnect stage2 suppressed");
      context.stream.reconnect.recover_last_action_us = now_us;
      LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage2_suppressed reason=%s attempts=%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           reason,
           context.stream.restart_source_attempts);
      return;
    }

    bool restart_ok = request_stream_restart_coordinated(
        "post_reconnect_stage2",
        RECONNECT_RECOVER_TARGET_KBPS,
        now_us);
    if (restart_ok) {
      context.stream.reconnect.recover_last_action_us = now_us;
      context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_SOFT_RESTARTED;
      if (context.active_host) {
        host_set_hint(context.active_host,
                      "Rebuilding stream at safer bitrate",
                      true,
                      HINT_DURATION_RECOVERY_US);
      }
      LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage2_soft_restart bitrate=%u fps=%u/%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           RECONNECT_RECOVER_TARGET_KBPS,
           incoming_fps,
           target_fps);
    } else {
      LOGE("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage2_soft_restart_failed",
           context.stream.session_generation,
           context.stream.reconnect_generation);
      reset_reconnect_recovery_state();
    }
    return;
  }

  if (context.stream.reconnect.recover_stage == RECONNECT_RECOVER_STAGE_SOFT_RESTARTED) {
    if (now_us - context.stream.reconnect.recover_last_action_us <
        RECONNECT_RECOVER_STAGE2_WAIT_US) {
      return;
    }
    if (context.stream.reconnect.recover_restart_attempts >= 1)
      return;

    bool restart_ok = request_stream_restart_coordinated(
        "post_reconnect_stage3",
        LOSS_RETRY_BITRATE_KBPS,
        now_us);
    if (restart_ok) {
      context.stream.reconnect.recover_last_action_us = now_us;
      context.stream.reconnect.recover_restart_attempts++;
      context.stream.reconnect.recover_stage = RECONNECT_RECOVER_STAGE_ESCALATED;
      if (context.active_host) {
        host_set_hint(context.active_host,
                      "Persistent video desync - rebuilding session",
                      true,
                      HINT_DURATION_RECOVERY_US);
      }
      LOGD("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage3_guarded_restart bitrate=%u fps=%u/%u",
           context.stream.session_generation,
           context.stream.reconnect_generation,
           LOSS_RETRY_BITRATE_KBPS,
           incoming_fps,
           target_fps);
    } else {
      LOGE("PIPE/RECOVER gen=%u reconnect_gen=%u action=stage3_guarded_restart_failed",
           context.stream.session_generation,
           context.stream.reconnect_generation);
      reset_reconnect_recovery_state();
    }
    return;
  }

  if (context.stream.reconnect.recover_stage > RECONNECT_RECOVER_STAGE_ESCALATED) {
    LOGE("PIPE/RECOVER gen=%u reconnect_gen=%u action=invalid_stage_reset stage=%u",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         context.stream.reconnect.recover_stage);
    reset_reconnect_recovery_state();
  }
}
