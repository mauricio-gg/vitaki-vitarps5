// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mic.h"

#if VITARPS5_DEBUG_TOOLS

#include "context.h"
#include "mic_frame.h"

#include <chiaki/opusencoder.h>
#include <opus/opus.h>
#include <psp2/audioin.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>

/* Lower priority than decode (64), audio (64) and input (96): a higher number is a lower
 * priority. No CPU pinning (mask 0, like the input thread): the scheduler may put it anywhere, and
 * at 128 it gives way to the video and audio threads on USER_1 and USER_2. */
#define MIC_THREAD_PRIORITY 128
#define MIC_THREAD_STACK_BYTES (128 * 1024)
#define MIC_IDLE_SLEEP_US 20000
#define MIC_STATS_PERIOD_US 1000000ULL
#define MIC_GRAIN_MAX 512

/* The header we hand the encoder is chiaki-ng's: 2 channels, 16 bit, 48 kHz, 480 per frame. (The
 * core's own stream_connection_enable_microphone() declares the format to the console with the
 * channels and bits arguments of chiaki_audio_header_set() the other way round; not touched.) */
#define MIC_ENC_CHANNELS 2
#define MIC_ENC_BITS 16
#define MIC_ENC_RATE 48000

typedef struct mic_open_try_t {
  SceAudioInPortType type;
  int freq;
  int grain;
  int upsample;
  const char *name;
} MicOpenTry;

/* 48 kHz RAW first; if the firmware refuses it, the 16 kHz VOICE port stretched 3x. */
static const MicOpenTry open_tries[] = {
    {SCE_AUDIO_IN_PORT_TYPE_RAW, 48000, 480, 1, "RAW"},
    {SCE_AUDIO_IN_PORT_TYPE_RAW, 48000, 512, 1, "RAW"},
    {SCE_AUDIO_IN_PORT_TYPE_RAW, 48000, 256, 1, "RAW"},
    {SCE_AUDIO_IN_PORT_TYPE_VOICE, 16000, 160, 3, "VOICE"},
    {SCE_AUDIO_IN_PORT_TYPE_VOICE, 16000, 256, 3, "VOICE"},
    {SCE_AUDIO_IN_PORT_TYPE_VOICE, 16000, 128, 3, "VOICE"},
};
#define OPEN_TRY_COUNT ((int)(sizeof(open_tries) / sizeof(open_tries[0])))

/* The UI thread writes want_on and the lifecycle fields (under lifecycle_lock); the capture thread
 * reads want_on and may clear it when something fails. */
static volatile bool want_on = false;
static volatile bool should_exit = false;
static volatile bool thread_dead = false;
static bool thread_started = false;
static SceUID thread_id = -1;
static volatile int lifecycle_lock = 0;

/* Capture thread only, all allocated once (static). */
static ChiakiOpusEncoder enc;
static bool enc_live = false;
static MicFrame frame_builder;
static int16_t grain_buf[MIC_GRAIN_MAX];
static int chosen_try = -1; /* open_tries index that worked, kept for later reopen */

typedef struct mic_stats_t {
  uint32_t grains, frames, enc_calls;
  uint64_t enc_sum_us, enc_max_us, cap_sum_us, cap_max_us;
} MicStats;
static MicStats stats;

static void lock_lifecycle(void) {
  while (__sync_lock_test_and_set(&lifecycle_lock, 1))
    sceKernelDelayThread(1000);
}

static void unlock_lifecycle(void) {
  __sync_lock_release(&lifecycle_lock);
}

bool vitamic_is_on(void) {
  return want_on;
}

/* chiaki_session_toggle_microphone()'s argument is inverted against its name: the wire byte for
 * "true" is 0, which chiaki-ng sends to START sending (its first call passes muted == true and it
 * then marks itself unmuted), while the core's own start-of-stream call passes false, which is
 * "mic muted". So on == true here. */
static bool send_mic_state(bool on) {
  ChiakiErrorCode err = chiaki_session_toggle_microphone(&context.stream.session, on);
  if (err != CHIAKI_ERR_SUCCESS) {
    LOGE("MIC/FAIL toggle on=%d err=%d \"%s\"", on ? 1 : 0, err, chiaki_error_string(err));
    return false;
  }
  LOGD("MIC/CTRL toggle sent on=%d (lib arg=%d)", on ? 1 : 0, on ? 1 : 0);
  return true;
}

static void encoder_teardown(void) {
  if (!enc_live)
    return;
  /* chiaki_opus_encoder_fini() frees the sender and frame buffer but not the Opus encoder. */
  opus_encoder_destroy(enc.opus_encoder);
  enc.opus_encoder = NULL;
  chiaki_opus_encoder_fini(&enc);
  enc_live = false;
}

static bool encoder_start(void) {
  chiaki_opus_encoder_init(&enc, &context.log);
  enc_live = true;
  ChiakiAudioHeader header;
  chiaki_audio_header_set(&header, MIC_ENC_CHANNELS, MIC_ENC_BITS, MIC_ENC_RATE, MIC_FRAME_SAMPLES);
  chiaki_opus_encoder_header(&header, &enc, &context.stream.session);
  if (!enc.opus_encoder || !enc.audio_sender) {
    LOGE("MIC/FAIL encoder init opus=%p sender=%p", (void *)enc.opus_encoder,
         (void *)enc.audio_sender);
    encoder_teardown();
    return false;
  }
  return true;
}

/* Opens the first capture port the firmware accepts. Returns the port or a negative SCE error. */
static int open_port(const MicOpenTry **opened) {
  int first = chosen_try >= 0 ? chosen_try : 0;
  int last = chosen_try >= 0 ? chosen_try + 1 : OPEN_TRY_COUNT;
  int last_err = 0;
  for (int i = first; i < last; i++) {
    const MicOpenTry *t = &open_tries[i];
    int port = sceAudioInOpenPort(t->type, t->grain, t->freq, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    if (port >= 0) {
      chosen_try = i;
      *opened = t;
      return port;
    }
    last_err = port;
    LOGE("MIC/FAIL open port=%s rate=%d grain=%d err=0x%08X", t->name, t->freq, t->grain,
         (unsigned)port);
  }
  return last_err;
}

static void on_frame(int16_t *pcm, void *user) {
  (void)user;
  uint64_t t0 = sceKernelGetProcessTimeWide();
  chiaki_opus_encoder_frame(pcm, &enc);
  uint64_t dt = sceKernelGetProcessTimeWide() - t0;
  stats.enc_calls++;
  stats.enc_sum_us += dt;
  if (dt > stats.enc_max_us)
    stats.enc_max_us = dt;
}

static void log_stats(uint16_t *last_frame_index) {
  uint16_t idx = (uint16_t)enc.audio_sender->frame_index;
  uint16_t sent = (uint16_t)(idx - *last_frame_index);
  *last_frame_index = idx;
  uint32_t calls = stats.enc_calls ? stats.enc_calls : 1;
  uint32_t grains = stats.grains ? stats.grains : 1;
  LOGD(
      "MIC/STATS frames_captured=%u frames_encoded=%u packets_sent=%u enc_avg_us=%u enc_max_us=%u "
      "grains=%u cap_block_avg_us=%u cap_block_max_us=%u",
      stats.frames, stats.enc_calls, sent, (unsigned)(stats.enc_sum_us / calls),
      (unsigned)stats.enc_max_us, stats.grains, (unsigned)(stats.cap_sum_us / grains),
      (unsigned)stats.cap_max_us);
  memset(&stats, 0, sizeof(stats));
}

static int mic_thread_func(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  bool connected = false; /* chiaki_session_connect_microphone() done for this session */
  bool sending = false;
  int port = -1;
  const MicOpenTry *opened = NULL;
  uint16_t last_frame_index = 0;
  uint64_t stats_at_us = 0;

  if (!encoder_start()) {
    want_on = false;
    thread_dead = true;
    return 0;
  }

  while (!should_exit) {
    const bool want = want_on && !context.stream.stop_requested;

    if (want && !sending) {
      port = open_port(&opened);
      if (port < 0) {
        LOGE("MIC/FAIL no capture port could be opened, last err=0x%08X", (unsigned)port);
        want_on = false;
        port = -1;
        continue;
      }
      if (!connected) {
        ChiakiErrorCode err = chiaki_session_connect_microphone(&context.stream.session);
        if (err != CHIAKI_ERR_SUCCESS) {
          LOGE("MIC/FAIL connect_microphone err=%d \"%s\"", err, chiaki_error_string(err));
          sceAudioInReleasePort(port);
          port = -1;
          want_on = false;
          continue;
        }
        connected = true;
      }
      if (!send_mic_state(true)) {
        sceAudioInReleasePort(port);
        port = -1;
        want_on = false;
        continue;
      }
      mic_frame_init(&frame_builder, opened->upsample);
      memset(&stats, 0, sizeof(stats));
      last_frame_index = (uint16_t)enc.audio_sender->frame_index;
      stats_at_us = sceKernelGetProcessTimeWide();
      sending = true;
      LOGD("MIC/START port=%s rate=%d grain=%d upsample=%d prio=%d affinity=default", opened->name,
           opened->freq, opened->grain, opened->upsample, MIC_THREAD_PRIORITY);
    } else if (!want && sending) {
      /* On a stop the session is going away: skip the control message. */
      if (!context.stream.stop_requested)
        send_mic_state(false);
      sceAudioInReleasePort(port);
      port = -1;
      sending = false;
      LOGD("MIC/STOP capture closed");
    }

    if (!sending) {
      sceKernelDelayThread(MIC_IDLE_SLEEP_US);
      continue;
    }

    uint64_t t0 = sceKernelGetProcessTimeWide();
    int r = sceAudioInInput(port, grain_buf);
    uint64_t blocked = sceKernelGetProcessTimeWide() - t0;
    if (r < 0) {
      LOGE("MIC/FAIL sceAudioInInput err=0x%08X", (unsigned)r);
      want_on = false; /* the next pass closes the port and mutes */
      continue;
    }
    stats.grains++;
    stats.cap_sum_us += blocked;
    if (blocked > stats.cap_max_us)
      stats.cap_max_us = blocked;
    stats.frames +=
        (uint32_t)mic_frame_push(&frame_builder, grain_buf, (size_t)opened->grain, on_frame, NULL);

    uint64_t now = sceKernelGetProcessTimeWide();
    if (now - stats_at_us >= MIC_STATS_PERIOD_US) {
      stats_at_us = now;
      log_stats(&last_frame_index);
    }
  }

  if (port >= 0)
    sceAudioInReleasePort(port);
  encoder_teardown();
  thread_dead = true;
  return 0;
}

/* Why a toggle must be refused right now, or NULL. The encoder binds to the connected stream, so
 * the mic needs a flowing one, like a resync tap. */
static const char *toggle_refuse_reason(void) {
  if (!context.stream.session_init || !context.stream.is_streaming ||
      !context.stream.video_first_frame_logged)
    return "not streaming";
  if (context.stream.stop_requested || context.stream.teardown_in_progress)
    return "stop or teardown in progress";
  return NULL;
}

static void join_thread_locked(void) {
  if (!thread_started)
    return;
  should_exit = true;
  SceInt32 ret = 0;
  sceKernelWaitThreadEnd(thread_id, &ret, NULL);
  sceKernelDeleteThread(thread_id);
  thread_started = false;
  thread_id = -1;
  should_exit = false;
  thread_dead = false;
}

void vitamic_request_toggle(uint8_t source) {
  const char *how = source == DEBUG_PANEL_REQUEST_COMBO ? "combo" : "touch";
  lock_lifecycle();
  const char *refuse = toggle_refuse_reason();
  if (refuse) {
    LOGD("MIC/TOGGLE ignored source=%s reason=\"%s\"", how, refuse);
    unlock_lifecycle();
    return;
  }
  /* A thread that gave up (encoder or port failure) is reaped so this toggle can retry. */
  if (thread_started && thread_dead)
    join_thread_locked();
  const bool on = !want_on;
  LOGD("MIC/TOGGLE %s source=%s", on ? "on" : "off", how);
  want_on = on;
  if (on && !thread_started) {
    thread_id = sceKernelCreateThread("VitaMicThread", mic_thread_func, MIC_THREAD_PRIORITY,
                                      MIC_THREAD_STACK_BYTES, 0, 0, NULL);
    if (thread_id < 0) {
      LOGE("MIC/FAIL create thread err=0x%08X", (unsigned)thread_id);
      want_on = false;
    } else {
      int st = sceKernelStartThread(thread_id, 0, NULL);
      if (st < 0) {
        LOGE("MIC/FAIL start thread err=0x%08X", (unsigned)st);
        sceKernelDeleteThread(thread_id);
        thread_id = -1;
        want_on = false;
      } else {
        thread_started = true;
      }
    }
  }
  unlock_lifecycle();
}

void vitamic_stop(void) {
  lock_lifecycle();
  if (thread_started)
    LOGD("MIC/STOP stream teardown, joining capture thread");
  join_thread_locked();
  want_on = false;
  unlock_lifecycle();
}

#endif /* VITARPS5_DEBUG_TOOLS */
