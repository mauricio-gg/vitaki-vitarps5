#include "context.h"
#include "config.h"
#include "controller.h"
#include "host.h"
#include "host_input.h"
#include "host_feedback.h"
#include "host_metrics.h"
#include "host_lifecycle.h"
#include "host_callbacks.h"
#include "discovery.h"
#include "audio.h"
#include "video.h"
#include "string.h"
#include <psp2/kernel/processmgr.h>
#include <chiaki/session.h>

static void request_stream_stop(const char *reason);

#define HINT_DURATION_LINK_WAIT_US (3 * 1000 * 1000ULL)
#define HINT_DURATION_CREDENTIAL_US (7 * 1000 * 1000ULL)

static bool host_mac_is_zero(const uint8_t mac[6]) {
  if (!mac)
    return true;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0)
      return false;
  }
  return true;
}

static bool host_try_hydrate_registered_state_from_config(VitaChiakiHost *host) {
  if (!host)
    return false;
  if (host->registered_state)
    return true;

  VitaChiakiHost *matched = NULL;
  bool host_has_mac = !host_mac_is_zero(host->server_mac);
  for (int i = 0; i < context.config.num_registered_hosts; i++) {
    VitaChiakiHost *candidate = context.config.registered_hosts[i];
    if (!candidate || !candidate->registered_state)
      continue;

    if (host_has_mac && mac_addrs_match(&candidate->server_mac, &host->server_mac)) {
      matched = candidate;
      break;
    }
  }

  if (!matched && host->hostname && host->hostname[0]) {
    for (int i = 0; i < context.config.num_registered_hosts; i++) {
      VitaChiakiHost *candidate = context.config.registered_hosts[i];
      if (!candidate || !candidate->registered_state || !candidate->hostname)
        continue;
      if (strcmp(candidate->hostname, host->hostname) == 0) {
        matched = candidate;
        break;
      }
    }
  }

  if (!matched || !matched->registered_state)
    return false;

  host->registered_state = malloc(sizeof(ChiakiRegisteredHost));
  if (!host->registered_state) {
    LOGE("Out of memory while hydrating host credentials");
    return false;
  }
  copy_host_registered_state(host->registered_state, matched->registered_state);
  if (host_mac_is_zero(host->server_mac))
    memcpy(host->server_mac, matched->server_mac, sizeof(host->server_mac));

  LOGD("Hydrated missing registered_state for host %s from persisted config",
       host->hostname ? host->hostname : "<null>");
  return true;
}

void host_free(VitaChiakiHost *host) {
  if (host) {
    if (host->discovery_state) {
      destroy_discovery_host(host->discovery_state);
      host->discovery_state = NULL;
    }
    if (host->registered_state) {
      free(host->registered_state);
    }
    if (host->hostname) {
      free(host->hostname);
    }
  }
}

static void request_stream_stop(const char *reason) {
  if (!context.stream.session_init)
    return;
  bool user_stop = false;
  if (reason && (strcmp(reason, "user cancel") == 0 ||
                 strcmp(reason, "L+R+Start") == 0)) {
    user_stop = true;
  }
  if (!context.stream.stop_requested) {
    LOGD("Stopping stream (%s)", reason ? reason : "user");
    context.stream.stop_requested = true;
    context.stream.stop_requested_by_user = user_stop;
    if (user_stop) {
      context.stream.reset_reconnect_gen = true;
    }
  }
  context.stream.teardown_in_progress = true;
  context.stream.next_stream_allowed_us = 0;
  chiaki_session_stop(&context.stream.session);
}

void host_cancel_stream_request(void) {
  request_stream_stop("user cancel");
}

void host_request_stream_stop_from_input(const char *reason) {
  request_stream_stop(reason);
}

int host_stream(VitaChiakiHost* host) {
  LOGD("Preparing to start host_stream");
  if (!host || !host->hostname || !host->hostname[0]) {
    return 1;
  }
  if (!host_try_hydrate_registered_state_from_config(host)) {
    LOGE("Missing credentials for host %s; cannot start stream",
         host->hostname ? host->hostname : "<null>");
    host_set_hint(host,
                  "Missing console credentials. Re-pair may be required.",
                  true,
                  HINT_DURATION_CREDENTIAL_US);
    return 1;
  }
  // Drain any pending deferred finalization before starting a new session.
  // Without this, a rapid reconnect could overwrite the session struct while
  // the old session thread is still running (race between event_cb clearing
  // session_init and the UI thread running host_finalize_deferred_session).
  if (context.stream.session_finalize_pending) {
    LOGD("Deferred finalization pending; draining before new session");
    host_finalize_deferred_session();
    LOGD("Deferred finalization drain completed");
  }
  if (context.stream.session_init) {
    LOGD("Stream already initialized; ignoring duplicate start request");
    return 1;
  }
  bool discovery_was_running = context.discovery_enabled;
  context.discovery_resume_after_stream = false;
  host_set_hint(host, NULL, false, 0);

  int result = 1;
  bool resume_inputs = context.stream.inputs_resume_pending;
  context.stream.stop_requested = false;
  context.stream.stop_requested_by_user = false;
  context.stream.teardown_in_progress = false;
  context.stream.inputs_ready = false;
  context.stream.is_streaming = false;
  context.stream.media_initialized = false;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (context.stream.retry_holdoff_active &&
      now_us >= context.stream.retry_holdoff_until_us) {
    LOGD("Retry holdoff expired (duration=%u ms)", context.stream.retry_holdoff_ms);
    context.stream.retry_holdoff_active = false;
    context.stream.retry_holdoff_ms = 0;
    context.stream.retry_holdoff_until_us = 0;
  }
  if (context.stream.next_stream_allowed_us &&
      now_us < context.stream.next_stream_allowed_us) {
    uint64_t remaining_ms =
        (context.stream.next_stream_allowed_us - now_us + 999) / 1000;
    if (context.stream.retry_holdoff_active &&
        now_us < context.stream.retry_holdoff_until_us) {
      uint64_t holdoff_remaining_ms =
          (context.stream.retry_holdoff_until_us - now_us + 999) / 1000;
      LOGD("Stream start blocked by adaptive holdoff for %llu ms (total cooldown %llu ms)",
           holdoff_remaining_ms, remaining_ms);
    } else {
      LOGD("Stream start blocked for %llu ms to let console recover", remaining_ms);
    }
    goto cleanup;
  }

  ChiakiVideoResolutionPreset requested_resolution = context.config.resolution;
  // Defensive guardrail: config/UI path should already normalize unsupported values,
  // but force a safe profile here to preserve stream startup reliability.
  if (requested_resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p ||
      requested_resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p) {
    LOGD("Requested legacy unsupported %s profile; forcing 540p fallback",
         requested_resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p ? "1080p" : "720p");
    requested_resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  }

  ChiakiConnectVideoProfile profile = {};
	chiaki_connect_video_profile_preset(&profile,
			requested_resolution, context.config.fps);
  LOGD("Bitrate policy: preset_default (%u kbps @ %ux%u)",
       profile.bitrate, profile.width, profile.height);
  if (context.stream.loss_retry_active && context.stream.loss_retry_bitrate_kbps > 0) {
    profile.bitrate = context.stream.loss_retry_bitrate_kbps;
    LOGD("Applying packet-loss fallback bitrate: %u kbps", profile.bitrate);
    context.stream.loss_retry_active = false;
  }
  ui_connection_set_stage(UI_CONNECTION_STAGE_CONNECTING);
  ChiakiConnectInfo chiaki_connect_info = {};
	chiaki_connect_info.host = host->hostname;
	chiaki_connect_info.video_profile = profile;
	chiaki_connect_info.video_profile_auto_downgrade = true;
	chiaki_connect_info.send_actual_start_bitrate = context.config.send_actual_start_bitrate;
	chiaki_connect_info.ps5 = chiaki_target_is_ps5(host->target);
	memcpy(chiaki_connect_info.regist_key, host->registered_state->rp_regist_key, sizeof(chiaki_connect_info.regist_key));
	memcpy(chiaki_connect_info.morning, host->registered_state->rp_key, sizeof(chiaki_connect_info.morning));
  if (context.stream.cached_controller_valid) {
    chiaki_connect_info.cached_controller_state = context.stream.cached_controller_state;
    chiaki_connect_info.cached_controller_state_valid = true;
  } else {
	chiaki_controller_state_set_idle(&chiaki_connect_info.cached_controller_state);
	chiaki_connect_info.cached_controller_state_valid = false;
	}

	LOGD("Initializing Chiaki session (host=%s, bitrate=%u kbps, fps=%u)",
	     host->hostname ? host->hostname : "<null>",
	     profile.bitrate,
	     profile.max_fps);
  LOGD("Recovery profile: stable_default");

	ChiakiErrorCode err = chiaki_session_init(&context.stream.session, &chiaki_connect_info, &context.log);
	if(err != CHIAKI_ERR_SUCCESS) {
    if (err == CHIAKI_ERR_PARSE_ADDR) {
      LOGE("Error during stream setup: console address unresolved; keeping discovery active");
      host_set_hint(host, "Waiting for console network link...", false, HINT_DURATION_LINK_WAIT_US);
    } else {
		  LOGE("Error during stream setup: %s", chiaki_error_string(err));
    }
    goto cleanup;
  }
  if (resume_inputs && err == CHIAKI_ERR_SUCCESS) {
    context.stream.inputs_ready = true;
    context.stream.inputs_resume_pending = false;
  }

  uint32_t new_generation = context.stream.session_generation + 1;
  if (context.stream.reset_reconnect_gen) {
    context.stream.reconnect_generation = 0;
    // Don't clear flag here — clear it when streaming actually starts.
    // This ensures the flag survives RP_IN_USE retry cycles so all
    // subsequent auto-retries also get reconnect_gen=0.
  } else {
    context.stream.reconnect_generation =
        context.stream.session_generation > 0 ? context.stream.session_generation : 0;
  }
  context.stream.session_generation = new_generation;
  LOGD("PIPE/SESSION start gen=%u reconnect_gen=%u host=%s",
       context.stream.session_generation,
       context.stream.reconnect_generation,
       host->hostname ? host->hostname : "<null>");

  if (discovery_was_running) {
    LOGD("Suspending discovery during stream");
    stop_discovery(true);
    context.discovery_resume_after_stream = true;
  }
	init_controller_map(&(context.stream.vcmi), context.config.controller_map_id);
	// Mark session as initialized - MUST use mutex
	chiaki_mutex_lock(&context.stream.finalization_mutex);
 	context.stream.session_init = true;
	chiaki_mutex_unlock(&context.stream.finalization_mutex);
	host_metrics_reset_stream(false);
  uint32_t negotiated = profile.max_fps;
  if (negotiated == 0)
    negotiated = 60;
  context.stream.negotiated_fps = negotiated;
  uint32_t clamp_fps = negotiated;
  if (context.config.force_30fps)
    clamp_fps = clamp_fps > 30 ? 30 : clamp_fps;
  context.stream.target_fps = clamp_fps;
  context.stream.measured_incoming_fps = 0;
  context.stream.fps_window_start_us = 0;
  context.stream.fps_window_frame_count = 0;
  context.stream.pacing_accumulator = 0;
	LOGD("Chiaki session initialized successfully, starting media pipeline");
	ChiakiAudioSink audio_sink;
	chiaki_opus_decoder_init(&context.stream.opus_decoder, &context.log);
	chiaki_opus_decoder_set_cb(&context.stream.opus_decoder, vita_audio_init, vita_audio_cb, NULL);
	chiaki_opus_decoder_get_sink(&context.stream.opus_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&context.stream.session, &audio_sink);
  context.stream.media_initialized = true;
	chiaki_session_set_video_sample_cb(&context.stream.session, host_video_cb, NULL);
	chiaki_session_set_event_cb(&context.stream.session, host_event_cb, NULL);
	chiaki_controller_state_set_idle(&context.stream.controller_state);

  err = vita_h264_setup(profile.width, profile.height);
  if (err != 0) {
		LOGE("Error during video start: %d (0x%08x), profile=%ux%u@%u",
         err, (unsigned int)err, profile.width, profile.height, profile.max_fps);
    goto cleanup;
  }
  vita_h264_start();

  err = chiaki_session_start(&context.stream.session);
  if(err != CHIAKI_ERR_SUCCESS) {
		LOGE("Error during stream start: %s", chiaki_error_string(err));
    goto cleanup;
  }

	context.stream.input_thread_should_exit = false;
	err = chiaki_thread_create(&context.stream.input_thread, host_input_thread_func, &context.stream);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		LOGE("Failed to create input thread");
	}

  result = 0;

cleanup:
  if (result != 0) {
    context.stream.inputs_resume_pending = false;
    host_shutdown_media_pipeline();
    // Finalize if session was partially initialized
    if (context.stream.session_init) {
      host_finalize_session_resources();
    }
    // No else needed - flag is already false or will be cleared by finalize
    context.stream.fast_restart_active = false;
    context.stream.reconnect_overlay_active = false;
    context.stream.loss_retry_active = false;
    context.stream.loss_retry_pending = false;
    context.stream.is_streaming = false;
    context.stream.inputs_ready = false;
    context.stream.teardown_in_progress = false;
    context.stream.session_finalize_pending = false;
    host_resume_discovery_if_needed();
    ui_connection_cancel();
  } else if (resume_inputs) {
    context.stream.inputs_ready = true;
    context.stream.inputs_resume_pending = false;
  }
  return result;
}
