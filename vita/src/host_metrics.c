#include "context.h"
#include "host_metrics.h"
#include "host_feedback.h"
#include "host_recovery.h"
#include "audio.h"
#include "video.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/net/netctl.h>
#include <string.h>

#define AV_DIAG_LOG_INTERVAL_US (5 * 1000 * 1000ULL)
#define AV_DIAG_STALE_SNAPSHOT_WARN_STREAK 5

void host_metrics_reset_stream(bool preserve_recovery_state) {
  context.stream.measured_bitrate_mbps = 0.0f;
  context.stream.measured_rtt_ms = 0;
  context.stream.last_rtt_refresh_us = 0;
  context.stream.metrics_last_update_us = 0;
  context.stream.retry_holdoff_ms = 0;
  context.stream.retry_holdoff_until_us = 0;
  context.stream.retry_holdoff_active = false;
  context.stream.video_first_frame_logged = false;
  context.stream.measured_incoming_fps = 0;
  context.stream.fps_under_target_windows = 0;
  context.stream.post_reconnect_low_fps_windows = 0;
  context.stream.post_reconnect_window_until_us = 0;
  context.stream.reconnect.recover_active = false;
  context.stream.reconnect.recover_stage = 0;
  context.stream.reconnect.recover_last_action_us = 0;
  context.stream.reconnect.recover_idr_attempts = 0;
  context.stream.reconnect.recover_restart_attempts = 0;
  context.stream.reconnect.recover_stable_windows = 0;
  context.stream.fps_window_start_us = 0;
  context.stream.fps_window_frame_count = 0;
  context.stream.negotiated_fps = 0;
  context.stream.target_fps = 0;
  context.stream.pacing_accumulator = 0;
  context.stream.frame_loss_events = 0;
  context.stream.total_frames_lost = 0;
  context.stream.loss_window_start_us = 0;
  context.stream.loss_window_event_count = 0;
  context.stream.loss_window_frame_accum = 0;
  context.stream.loss_burst_frame_accum = 0;
  context.stream.loss_counter_saturated_mask = 0;
  context.stream.loss_burst_start_us = 0;
  context.stream.loss_recovery_gate_hits = 0;
  context.stream.loss_recovery_window_start_us = 0;
  context.stream.last_loss_recovery_action_us = 0;
  context.stream.stream_start_us = 0;
  context.stream.loss_restart_soft_grace_until_us = 0;
  context.stream.loss_restart_grace_until_us = 0;
  context.stream.loss_alert_until_us = 0;
  context.stream.loss_alert_duration_us = 0;
  context.stream.logged_loss_events = 0;
  context.stream.auto_loss_downgrades = 0;
  context.stream.takion_drop_events = 0;
  context.stream.takion_drop_packets = 0;
  context.stream.logged_drop_events = 0;
  context.stream.takion_drop_last_us = 0;
  context.stream.av_diag.missing_ref_count = 0;
  context.stream.av_diag.corrupt_burst_count = 0;
  context.stream.av_diag.fec_fail_count = 0;
  context.stream.av_diag.sendbuf_overflow_count = 0;
  context.stream.av_diag.logged_missing_ref_count = 0;
  context.stream.av_diag.logged_corrupt_burst_count = 0;
  context.stream.av_diag.logged_fec_fail_count = 0;
  context.stream.av_diag.logged_sendbuf_overflow_count = 0;
  context.stream.av_diag.last_log_us = 0;
  context.stream.av_diag.last_corrupt_start = 0;
  context.stream.av_diag.last_corrupt_end = 0;
  context.stream.av_diag_stale_snapshot_streak = 0;
  context.stream.last_restart_failure_us = 0;
  context.stream.restart_handshake_failures = 0;
  context.stream.last_restart_handshake_fail_us = 0;
  context.stream.restart_cooloff_until_us = 0;
  context.stream.last_restart_source[0] = '\0';
  context.stream.restart_source_attempts = 0;

  // D1: Decode timing
  context.stream.decode_time_us = 0;
  context.stream.decode_avg_us = 0;
  context.stream.decode_max_us = 0;
  context.stream.decode_window_total_us = 0;
  context.stream.decode_window_max_us = 0;
  context.stream.decode_window_count = 0;

  // D4: Windowed bitrate
  context.stream.bitrate_prev_bytes = 0;
  context.stream.bitrate_prev_frames = 0;
  memset(context.stream.bitrate_window_delta_bytes, 0, sizeof(context.stream.bitrate_window_delta_bytes));
  memset(context.stream.bitrate_window_delta_frames, 0, sizeof(context.stream.bitrate_window_delta_frames));
  context.stream.bitrate_window_index = 0;
  context.stream.bitrate_window_filled = 0;
  context.stream.windowed_bitrate_mbps = 0.0f;

  // D5: Frame overwrite
  context.stream.frame_overwrite_count = 0;

  // D6: Wi-Fi RSSI
  context.stream.wifi_rssi = -1;

  // D7: Display FPS
  context.stream.display_fps = 0;
  context.stream.display_frame_count = 0;
  context.stream.display_fps_window_start_us = 0;

  // Stuck bitrate detection (streak resets always; once-per-session flag
  // survives fast restarts so we don't re-trigger after our own restart)
  context.stream.stuck_bitrate_low_fps_streak = 0;
  if (!preserve_recovery_state)
    context.stream.stuck_bitrate_restart_used = false;

  // Cascade alarm (streak resets always; once-per-session flag
  // survives fast restarts so we don't re-trigger after our own restart)
  context.stream.cascade_prev_missing_ref_count = 0;
  context.stream.cascade_alarm_streak = 0;
  if (!preserve_recovery_state)
    context.stream.cascade_alarm_restart_used = false;
  context.stream.cascade_alarm_last_action_us = 0;

  context.stream.disconnect_reason[0] = '\0';
  context.stream.disconnect_banner_until_us = 0;
  context.stream.loss_retry_pending = false;
  context.stream.loss_retry_active = false;
  context.stream.loss_retry_attempts = 0;
  context.stream.loss_retry_bitrate_kbps = 0;
  context.stream.loss_retry_ready_us = 0;
  context.stream.reconnect_overlay_active = false;
  context.stream.reconnect_overlay_start_us = 0;
  context.stream.fast_restart_active = false;
  context.stream.cached_controller_valid = false;
  context.stream.last_input_packet_us = 0;
  context.stream.last_input_stall_log_us = 0;
  context.stream.inputs_blocked_since_us = 0;
  context.stream.inputs_resume_pending = false;
  context.stream.unrecovered_frame_streak = 0;
  context.stream.unrecovered_gate_events = 0;
  context.stream.unrecovered_gate_window_start_us = 0;
  context.stream.unrecovered_persistent_events = 0;
  context.stream.unrecovered_persistent_window_start_us = 0;
  context.stream.unrecovered_idr_requests = 0;
  context.stream.unrecovered_idr_window_start_us = 0;
  context.stream.restart_failure_active = false;
  context.stream.auto_reconnect_count = 0;
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
  vitavideo_hide_poor_net_indicator();
}

void host_metrics_update_latency(void) {
  static const uint64_t RTT_REFRESH_INTERVAL_US = 1000000ULL;

  if (!context.stream.session_init)
    return;

  ChiakiStreamConnection *stream_connection = &context.stream.session.stream_connection;
  ChiakiVideoReceiver *receiver = stream_connection->video_receiver;
  if (!receiver)
    return;

  uint32_t takion_drop_events = context.stream.takion_drop_events;
  uint32_t takion_drop_packets = context.stream.takion_drop_packets;
  uint64_t takion_drop_last_us = context.stream.takion_drop_last_us;
  uint32_t av_diag_missing_ref_count = context.stream.av_diag.missing_ref_count;
  uint32_t av_diag_corrupt_burst_count = context.stream.av_diag.corrupt_burst_count;
  uint32_t av_diag_fec_fail_count = context.stream.av_diag.fec_fail_count;
  uint32_t av_diag_sendbuf_overflow_count = context.stream.av_diag.sendbuf_overflow_count;
  uint32_t av_diag_trylock_failures = 0;
  uint32_t av_diag_last_corrupt_start = context.stream.av_diag.last_corrupt_start;
  uint32_t av_diag_last_corrupt_end = context.stream.av_diag.last_corrupt_end;
  bool diag_snapshot_stale = true;

  // Snapshot diagnostics under dedicated diagnostics mutex so hot packet
  // paths do not contend with stream state transitions.
  if (chiaki_mutex_trylock(&stream_connection->diag_mutex) == CHIAKI_ERR_SUCCESS) {
    takion_drop_events = stream_connection->drop_events;
    takion_drop_packets = stream_connection->drop_packets;
    takion_drop_last_us =
        stream_connection->drop_last_ms ? (stream_connection->drop_last_ms * 1000ULL) : 0;
    av_diag_missing_ref_count = stream_connection->av_missing_ref_events;
    av_diag_corrupt_burst_count = stream_connection->av_corrupt_burst_events;
    av_diag_fec_fail_count = stream_connection->av_fec_fail_events;
    av_diag_sendbuf_overflow_count = stream_connection->av_sendbuf_overflow_events;
    av_diag_trylock_failures = stream_connection->diag_trylock_failures;
    av_diag_last_corrupt_start = stream_connection->av_last_corrupt_start;
    av_diag_last_corrupt_end = stream_connection->av_last_corrupt_end;
    chiaki_mutex_unlock(&stream_connection->diag_mutex);
    diag_snapshot_stale = false;
  }

  if (diag_snapshot_stale) {
    if (context.stream.av_diag_stale_snapshot_streak < UINT32_MAX)
      context.stream.av_diag_stale_snapshot_streak++;
  } else {
    context.stream.av_diag_stale_snapshot_streak = 0;
  }

  context.stream.takion_drop_events = takion_drop_events;
  context.stream.takion_drop_packets = takion_drop_packets;
  context.stream.takion_drop_last_us = takion_drop_last_us;
  context.stream.av_diag.missing_ref_count = av_diag_missing_ref_count;
  context.stream.av_diag.corrupt_burst_count = av_diag_corrupt_burst_count;
  context.stream.av_diag.fec_fail_count = av_diag_fec_fail_count;
  context.stream.av_diag.sendbuf_overflow_count = av_diag_sendbuf_overflow_count;
  context.stream.av_diag.last_corrupt_start = av_diag_last_corrupt_start;
  context.stream.av_diag.last_corrupt_end = av_diag_last_corrupt_end;

  uint32_t fps = context.stream.session.connect_info.video_profile.max_fps;
  if (fps == 0)
    fps = 30;

  ChiakiStreamStats *stats = &receiver->frame_processor.stream_stats;
  uint64_t bitrate_bps = chiaki_stream_stats_bitrate(stats, fps);
  float bitrate_mbps = bitrate_bps > 0 ? ((float)bitrate_bps / 1000000.0f) : 0.0f;
  uint64_t now_us = sceKernelGetProcessTimeWide();

  context.stream.measured_bitrate_mbps = bitrate_mbps;

  // D4: Windowed bitrate — 3-element ring buffer for rolling 3s average
  {
    uint64_t total_bytes = stats->bytes;
    uint64_t total_frames = stats->frames;
    uint64_t delta_bytes = total_bytes - context.stream.bitrate_prev_bytes;
    uint32_t delta_frames = (uint32_t)(total_frames - context.stream.bitrate_prev_frames);
    context.stream.bitrate_prev_bytes = total_bytes;
    context.stream.bitrate_prev_frames = total_frames;

    uint8_t idx = context.stream.bitrate_window_index;
    context.stream.bitrate_window_delta_bytes[idx] = delta_bytes;
    context.stream.bitrate_window_delta_frames[idx] = delta_frames;
    context.stream.bitrate_window_index = (idx + 1) % 3;
    if (context.stream.bitrate_window_filled < 3)
      context.stream.bitrate_window_filled++;

    uint64_t sum_bytes = 0;
    uint32_t sum_frames = 0;
    for (uint8_t i = 0; i < context.stream.bitrate_window_filled; i++) {
      sum_bytes += context.stream.bitrate_window_delta_bytes[i];
      sum_frames += context.stream.bitrate_window_delta_frames[i];
    }
    if (sum_frames > 0 && fps > 0 && context.stream.bitrate_window_filled >= 2) {
      float window_bps = ((float)sum_bytes * 8.0f * (float)fps) / (float)sum_frames;
      float window_mbps = window_bps / 1000000.0f;
      if (window_mbps > 100.0f) window_mbps = 100.0f;  // sanity clamp: Vita Wi-Fi ceiling
      context.stream.windowed_bitrate_mbps = window_mbps;
    }
  }

  uint32_t effective_target_fps =
      context.stream.target_fps ? context.stream.target_fps :
      context.stream.negotiated_fps;
  uint32_t incoming_fps = context.stream.measured_incoming_fps;
  bool low_fps_window = effective_target_fps > 0 && incoming_fps > 0 &&
      incoming_fps + 5 < effective_target_fps;
  bool av_diag_progressed =
      av_diag_missing_ref_count >
          context.stream.av_diag.logged_missing_ref_count ||
      av_diag_corrupt_burst_count >
          context.stream.av_diag.logged_corrupt_burst_count ||
      av_diag_fec_fail_count >
          context.stream.av_diag.logged_fec_fail_count ||
      av_diag_sendbuf_overflow_count >
          context.stream.av_diag.logged_sendbuf_overflow_count;
  if (diag_snapshot_stale) {
    // Don't escalate based on stale snapshots when diagnostics couldn't be sampled.
    av_diag_progressed = false;
    if (context.stream.av_diag_stale_snapshot_streak >= AV_DIAG_STALE_SNAPSHOT_WARN_STREAK &&
        low_fps_window) {
      // Prolonged diagnostics contention plus low FPS is treated as AV distress
      // so recovery does not stay blind under sustained lock pressure.
      av_diag_progressed = true;
    }
  }

  bool refresh_rtt = context.stream.last_rtt_refresh_us == 0 ||
                     (now_us - context.stream.last_rtt_refresh_us) >= RTT_REFRESH_INTERVAL_US;
  if (refresh_rtt) {
    uint64_t base_rtt_ms64 = context.stream.session.rtt_us / 1000ULL;
    uint64_t jitter_us = stream_connection->takion.jitter_stats.jitter_us;
    uint64_t jitter_ms64 = jitter_us / 1000ULL;
    uint64_t effective_rtt_ms64 = base_rtt_ms64 + jitter_ms64;
    if (effective_rtt_ms64 > UINT32_MAX)
      effective_rtt_ms64 = UINT32_MAX;
    if (effective_rtt_ms64 == 0)
      effective_rtt_ms64 = base_rtt_ms64 > UINT32_MAX ? UINT32_MAX : base_rtt_ms64;

    context.stream.measured_rtt_ms = (uint32_t)effective_rtt_ms64;
    context.stream.last_rtt_refresh_us = now_us;
    context.stream.metrics_last_update_us = now_us;

    // D6: Probe Wi-Fi RSSI once per second
    {
      SceNetCtlInfo rssi_info;
      int rssi_ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &rssi_info);
      context.stream.wifi_rssi = (rssi_ret >= 0) ? (int32_t)rssi_info.rssi_percentage : -1;
    }

    // Count low-fps health once per metrics window (about 1 second), not per frame.
    if (low_fps_window) {
      context.stream.fps_under_target_windows++;
      if (context.stream.post_reconnect_window_until_us &&
          now_us <= context.stream.post_reconnect_window_until_us) {
        context.stream.post_reconnect_low_fps_windows++;
      }
    }

    host_recovery_handle_post_reconnect_degraded_mode(av_diag_progressed,
                                                      incoming_fps,
                                                      effective_target_fps,
                                                      low_fps_window,
                                                      now_us);
    // Keep diagnostics passive here; stability path avoids restart escalation.
  }

  if (!context.config.show_latency)
    return;

  static const uint64_t LOG_INTERVAL_US = 1000000;
  static uint64_t last_log_us = 0;
  if (now_us - last_log_us >= LOG_INTERVAL_US) {
    float target_mbps = context.stream.session.connect_info.video_profile.bitrate / 1000.0f;
    LOGD("Latency metrics — target %.2f Mbps, measured %.2f Mbps, RTT %u ms (base %u ms, jitter %llu us)",
         target_mbps,
         bitrate_mbps,
         context.stream.measured_rtt_ms,
         (uint32_t)(context.stream.session.rtt_us / 1000),
         (unsigned long long)stream_connection->takion.jitter_stats.jitter_us);
    LOGD("PIPE/FPS gen=%u reconnect_gen=%u incoming=%u target=%u low_windows=%u post_reconnect_low=%u post_window_remaining_ms=%llu decode_avg_ms=%.1f decode_max_ms=%.1f windowed_mbps=%.2f overwrites=%u rssi=%d display_fps=%u stuck_streak=%u stuck_used=%d cascade_streak=%u cascade_used=%d",
         context.stream.session_generation,
         context.stream.reconnect_generation,
         incoming_fps,
         effective_target_fps,
         context.stream.fps_under_target_windows,
         context.stream.post_reconnect_low_fps_windows,
         context.stream.post_reconnect_window_until_us &&
                 now_us < context.stream.post_reconnect_window_until_us
             ? (unsigned long long)((context.stream.post_reconnect_window_until_us -
                                     now_us) / 1000ULL)
             : 0ULL,
         context.stream.decode_avg_us / 1000.0f,
         context.stream.decode_max_us / 1000.0f,
         context.stream.windowed_bitrate_mbps,
         context.stream.frame_overwrite_count,
         context.stream.wifi_rssi,
         context.stream.display_fps,
         context.stream.stuck_bitrate_low_fps_streak,
         (int)context.stream.stuck_bitrate_restart_used,
         context.stream.cascade_alarm_streak,
         (int)context.stream.cascade_alarm_restart_used);
    last_log_us = now_us;
  }

  if (context.stream.takion_drop_events != context.stream.logged_drop_events) {
    uint32_t delta = context.stream.takion_drop_events - context.stream.logged_drop_events;
    LOGD("Packet loss — Takion dropped %u packet(s), total %u",
         delta,
         context.stream.takion_drop_packets);
    context.stream.logged_drop_events = context.stream.takion_drop_events;
    host_handle_takion_overflow();
  }

  bool av_diag_changed = av_diag_progressed;
  if (av_diag_changed ||
      (context.stream.av_diag.last_log_us == 0 ||
       now_us - context.stream.av_diag.last_log_us >= AV_DIAG_LOG_INTERVAL_US)) {
    LOGD("AV diag — missing_ref=%u, corrupt_bursts=%u, fec_fail=%u, sendbuf_overflow=%u, diag_trylock_failures=%u, stale_diag_streak=%u, last_corrupt=%u-%u",
         context.stream.av_diag.missing_ref_count,
         context.stream.av_diag.corrupt_burst_count,
         context.stream.av_diag.fec_fail_count,
         context.stream.av_diag.sendbuf_overflow_count,
         av_diag_trylock_failures,
         context.stream.av_diag_stale_snapshot_streak,
         context.stream.av_diag.last_corrupt_start,
         context.stream.av_diag.last_corrupt_end);
    context.stream.av_diag.logged_missing_ref_count =
        context.stream.av_diag.missing_ref_count;
    context.stream.av_diag.logged_corrupt_burst_count =
        context.stream.av_diag.corrupt_burst_count;
    context.stream.av_diag.logged_fec_fail_count =
        context.stream.av_diag.fec_fail_count;
    context.stream.av_diag.logged_sendbuf_overflow_count =
        context.stream.av_diag.sendbuf_overflow_count;
    context.stream.av_diag.last_log_us = now_us;
  }
}
