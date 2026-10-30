// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "mic.h"

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

/* The only capture mode that opens on hardware: the 48 kHz RAW port is refused with 0x80260102
 * (GH #467). 256-sample grains at 16 kHz are 768 samples at 48 kHz (see mic_frame.h). */
#define MIC_CAPTURE_RATE 16000
#define MIC_CAPTURE_GRAIN 256

/* The header we hand the encoder is chiaki-ng's: 2 channels, 16 bit, 48 kHz, 480 per frame. (The
 * core's own stream_connection_enable_microphone() declares the format to the console with the
 * channels and bits arguments of chiaki_audio_header_set() the other way round; not touched.) */
#define MIC_ENC_CHANNELS 2
#define MIC_ENC_BITS 16
#define MIC_ENC_RATE 48000

/* Set when a stream starts and consumed when the capture thread is created; cleared by
 * vitamic_stop() so a tearing-down stream cannot start it. Guarded by lifecycle_lock. */
static bool armed = false;
/* The player's choice. Written by the UI thread, read by the capture thread. */
static volatile bool muted = false;
/* Capture thread lifecycle, guarded by lifecycle_lock except the two volatile flags, which the
 * capture thread itself sets or reads. */
static volatile bool should_exit = false;
static volatile bool thread_dead = false;
/* The mic was meant to run this stream but its thread could not be created or started. Set by the
 * UI thread under lifecycle_lock, cleared when the next stream starts. */
static volatile bool start_failed = false;
static bool thread_started = false;
static SceUID thread_id = -1;
static volatile int lifecycle_lock = 0;

/* Capture thread only, all allocated once (static). */
static ChiakiOpusEncoder enc;
static bool enc_live = false;
static MicFrame frame_builder;
static int16_t grain_buf[MIC_CAPTURE_GRAIN];

typedef struct mic_stats_t {
  uint32_t grains, frames, enc_calls;
  uint64_t enc_sum_us, enc_max_us, cap_sum_us, cap_max_us;
  int size_min, size_max; /* opus_encode() result: bytes, or a negative error */
  uint32_t size_40;       /* frames of exactly 40 bytes, the only size the sender sends */
} MicStats;
static MicStats stats;

static void lock_lifecycle(void) {
  while (__sync_lock_test_and_set(&lifecycle_lock, 1))
    sceKernelDelayThread(1000);
}

static void unlock_lifecycle(void) {
  __sync_lock_release(&lifecycle_lock);
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

static void on_frame(int16_t *pcm, void *user) {
  (void)user;
  uint64_t t0 = sceKernelGetProcessTimeWide();
  chiaki_opus_encoder_frame(pcm, &enc);
  uint64_t dt = sceKernelGetProcessTimeWide() - t0;
  const int size = enc.last_encoded_size;
  if (!stats.enc_calls || size < stats.size_min)
    stats.size_min = size;
  if (!stats.enc_calls || size > stats.size_max)
    stats.size_max = size;
  if (size == 40)
    stats.size_40++;
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
      "grains=%u cap_block_avg_us=%u cap_block_max_us=%u pkt_bytes_min=%d pkt_bytes_max=%d "
      "pkt_40=%u",
      stats.frames, stats.enc_calls, sent, (unsigned)(stats.enc_sum_us / calls),
      (unsigned)stats.enc_max_us, stats.grains, (unsigned)(stats.cap_sum_us / grains),
      (unsigned)stats.cap_max_us, stats.size_min, stats.size_max, stats.size_40);
  memset(&stats, 0, sizeof(stats));
}

/* Capture thread. Sends while the mic is not muted; a mute releases the port and tells the
 * console, an unmute reopens it. Any failure ends the thread (logged as MIC/FAIL) and leaves the
 * mic off for the rest of this stream; the stream itself carries on. */
static int mic_thread_func(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  bool connected = false; /* chiaki_session_connect_microphone() done for this session */
  bool sending = false;
  int port = -1;
  uint16_t last_frame_index = 0;
  uint64_t stats_at_us = 0;

  if (!encoder_start()) {
    thread_dead = true;
    return 0;
  }

  while (!should_exit) {
    const bool want = !muted && !context.stream.stop_requested;

    if (want && !sending) {
      port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE, MIC_CAPTURE_GRAIN, MIC_CAPTURE_RATE,
                                SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
      if (port < 0) {
        LOGE("MIC/FAIL open port=VOICE rate=%d grain=%d err=0x%08X", MIC_CAPTURE_RATE,
             MIC_CAPTURE_GRAIN, (unsigned)port);
        port = -1;
        break;
      }
      if (!connected) {
        ChiakiErrorCode err = chiaki_session_connect_microphone(&context.stream.session);
        if (err != CHIAKI_ERR_SUCCESS) {
          LOGE("MIC/FAIL connect_microphone err=%d \"%s\"", err, chiaki_error_string(err));
          break;
        }
        connected = true;
      }
      if (!send_mic_state(true))
        break;
      mic_frame_init(&frame_builder);
      memset(&stats, 0, sizeof(stats));
      last_frame_index = (uint16_t)enc.audio_sender->frame_index;
      stats_at_us = sceKernelGetProcessTimeWide();
      sending = true;
      LOGD("MIC/START port=VOICE rate=%d grain=%d upsample=%d prio=%d affinity=default",
           MIC_CAPTURE_RATE, MIC_CAPTURE_GRAIN, MIC_FRAME_UPSAMPLE, MIC_THREAD_PRIORITY);
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
      break;
    }
    stats.grains++;
    stats.cap_sum_us += blocked;
    if (blocked > stats.cap_max_us)
      stats.cap_max_us = blocked;
    stats.frames +=
        (uint32_t)mic_frame_push(&frame_builder, grain_buf, MIC_CAPTURE_GRAIN, on_frame, NULL);

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

/* Why the thread must not start right now, or NULL. The encoder binds to the connected stream, so
 * the mic needs a flowing one. */
static const char *start_refuse_reason(void) {
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

void vitamic_on_stream_start(bool recovery_reconnect) {
  lock_lifecycle();
  armed = context.config.microphone;
  start_failed = false;
  if (!recovery_reconnect)
    muted = false;
  LOGD("MIC/ARM setting=%s muted=%d recovery_reconnect=%d", armed ? "on" : "off", muted ? 1 : 0,
       recovery_reconnect ? 1 : 0);
  unlock_lifecycle();
}

void vitamic_ui_tick(void) {
  if (!armed)
    return;
  lock_lifecycle();
  if (armed && !start_refuse_reason()) {
    armed = false; /* one attempt per stream: a failed start is not retried until the next one */
    thread_id = sceKernelCreateThread("VitaMicThread", mic_thread_func, MIC_THREAD_PRIORITY,
                                      MIC_THREAD_STACK_BYTES, 0, 0, NULL);
    if (thread_id < 0) {
      LOGE("MIC/FAIL create thread err=0x%08X", (unsigned)thread_id);
      thread_id = -1;
      start_failed = true;
    } else {
      int st = sceKernelStartThread(thread_id, 0, NULL);
      if (st < 0) {
        LOGE("MIC/FAIL start thread err=0x%08X", (unsigned)st);
        sceKernelDeleteThread(thread_id);
        thread_id = -1;
        start_failed = true;
      } else {
        thread_started = true;
      }
    }
  }
  unlock_lifecycle();
}

void vitamic_set_muted(bool new_muted) {
  if (muted == new_muted)
    return;
  muted = new_muted;
  LOGD("MIC/%s", new_muted ? "MUTE" : "UNMUTE");
}

MicState vitamic_state(void) {
  /* A thread that ended by itself (not because vitamic_stop() asked it to) is a failure. */
  if (start_failed || (thread_started && thread_dead && !should_exit))
    return MIC_STATE_FAILED;
  if (!thread_started)
    return MIC_STATE_OFF;
  return muted ? MIC_STATE_MUTED : MIC_STATE_LIVE;
}

void vitamic_stop(void) {
  lock_lifecycle();
  armed = false;
  if (thread_started)
    LOGD("MIC/STOP stream teardown, joining capture thread");
  join_thread_locked();
  unlock_lifecycle();
}
