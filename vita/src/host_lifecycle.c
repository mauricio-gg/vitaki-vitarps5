#include "context.h"
#include "discovery.h"
#include "audio.h"
#include "video.h"
#include "host_lifecycle.h"
#include "host.h"

#include <psp2/kernel/processmgr.h>

void host_shutdown_media_pipeline(void) {
  if (!context.stream.media_initialized)
    return;

  // Stop the video decode thread and clear the frame_ready flag BEFORE freeing
  // the texture. The UI thread renders decoded frames from
  // vita_video_render_latest_frame(); ensure it is no longer drawing
  // the texture when we free it.
  context.stream.is_streaming = false;
  vita_h264_stop();
  sceKernelDelayThread(2000);

  chiaki_opus_decoder_fini(&context.stream.opus_decoder);
  vita_h264_cleanup();
  vita_audio_cleanup();
  context.stream.media_initialized = false;
  context.stream.inputs_ready = false;
  context.stream.fast_restart_active = false;
  context.stream.reconnect_overlay_active = false;
}

void host_finalize_session_resources(void) {
  // Acquire mutex for atomic check-and-set operation
  chiaki_mutex_lock(&context.stream.finalization_mutex);

  if (!context.stream.session_init) {
    // Already finalized by another thread
    chiaki_mutex_unlock(&context.stream.finalization_mutex);
    return;
  }

  // Mark as finalized immediately while holding mutex
  context.stream.session_init = false;

  chiaki_mutex_unlock(&context.stream.finalization_mutex);

  LOGD("Finalizing session resources");

  context.stream.input_thread_should_exit = true;

  // Join input thread
  ChiakiErrorCode err = chiaki_thread_join(&context.stream.input_thread, NULL);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Failed to join input thread: %d", err);
  } else {
    LOGD("Input thread joined successfully");
  }

  // Finalize session
  chiaki_session_fini(&context.stream.session);
  LOGD("Session finalized");
}

void host_finalize_deferred_session(void) {
  if (!context.stream.session_finalize_pending)
    return;

  uint64_t join_start = sceKernelGetProcessTimeWide();
  LOGD("Deferred finalization: joining session thread");
  ChiakiErrorCode err = chiaki_session_join(&context.stream.session);
  uint64_t join_duration_us = sceKernelGetProcessTimeWide() - join_start;
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Session join failed: %d after %ju us (proceeding with fini)", err, join_duration_us);
  } else {
    LOGD("Session thread joined in %ju us", join_duration_us);
  }

  // Join input thread (may already be exited)
  err = chiaki_thread_join(&context.stream.input_thread, NULL);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("Input thread join failed: %d (deferred path)", err);
  } else {
    LOGD("Input thread joined (deferred path)");
  }

  chiaki_session_fini(&context.stream.session);
  LOGD("Session finalized (deferred path)");

  context.stream.session_finalize_pending = false;
  host_resume_discovery_if_needed();
}

void host_resume_discovery_if_needed(void) {
  if (context.discovery_resume_after_stream) {
    LOGD("Resuming discovery after stream");
    start_discovery(NULL, NULL);
    context.discovery_resume_after_stream = false;
  }
}
