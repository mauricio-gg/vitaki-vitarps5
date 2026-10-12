#include <string.h>  // memcpy
#include <stdlib.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <chiaki/thread.h>

#include "audio.h"
#include "audio_queue.h"
#include "context.h"
#include "recv_probe.h"

/* Sound output design (#416): vita_audio_cb runs on the Takion receive thread and only hands the
 * decoded frame to a queue (audio_queue.h); a dedicated "VitaAudio" thread owns the blocking
 * sceAudioOutOutput, so that call can no longer hold the receive thread back.
 *
 * Threads and what they write:
 *   receive thread: the queue (push, under audio_mtx), audio_frames_received/audio_catchup_*,
 *                   the occupancy window, the receive-thread probe accumulator
 *   audio thread:   the queue (pop, under audio_mtx), the PIPE/AUDIOOUT counters and window
 *   cleanup caller: everything, but only after the audio thread was joined */

#define DEVICE_FRAME_ALIGN_SAMPLES 64  // the hardware wants output blocks of a multiple of this

#define AUDIO_US_PER_SECOND 1000000ULL
#define AUDIO_MS_PER_SECOND 1000ULL
#define AUDIO_OCC_WINDOW_US AUDIO_US_PER_SECOND
#define AUDIO_OUT_WINDOW_US AUDIO_US_PER_SECOND

static int port = -1;
static int rate = -1;
static int channels = -1;

static size_t frame_size;             // # of samples per channel in one frame
static size_t frame_bytes;            // frame_size * sample_bytes
static size_t device_buffer_frames;   // # of frames handed to the hardware per output call
static size_t device_buffer_samples;  // # of samples per channel in one output call
static size_t sample_bytes;           // channels * sizeof(int16_t)

/* One-time init state. audio_init_attempted: the first frame arrived and init ran (successfully or
 * not). audio_disabled: init failed, later frames are discarded. did_secondary_init: init
 * succeeded; read by the UI thread through vita_audio_started(). */
static bool audio_init_attempted = false;
static bool audio_disabled = false;
static volatile bool did_secondary_init = false;

/* The hand-off. audio_mtx guards audio_queue and audio_exit; audio_cond wakes the audio thread
 * when a device buffer's worth is queued or exit is requested. play_buffer is the audio thread's
 * private copy of what it is outputting. All allocated once in audio_start(). */
static ChiakiMutex audio_mtx;
static ChiakiCond audio_cond;
static bool audio_sync_ready = false;
static ChiakiThread audio_thread;
static bool audio_thread_started = false;
static bool audio_exit = false;
static AudioQueue audio_queue;
static uint8_t *queue_storage = NULL;
static uint8_t *play_buffer = NULL;

/* Receive-thread-only session counters (read by the cleanup caller after the receive thread is
 * gone, for the end-of-session log line). */
static uint32_t audio_frames_received = 0;
static uint32_t audio_catchup_events = 0;
static uint32_t audio_catchup_dropped = 0;

/* Occupancy window of the hand-off queue, published into context.stream.audio_q_occ_avg/max about
 * once a second (receive thread only), mirroring video.c's decode queue window convention. */
static uint64_t audio_occ_window_start_us = 0;
static uint32_t audio_occ_window_sum = 0;
static uint32_t audio_occ_window_count = 0;
static uint32_t audio_occ_window_max = 0;

/* PIPE/AUDIOOUT counters of the window being filled. Audio thread only. */
typedef struct {
  uint64_t aout_us;
  uint32_t out_calls, out_frames, aout_max_us;
  uint32_t rest_n, rest_err;
  int32_t rest_min, rest_max;
  uint32_t out_dry, out_gap_max_us, out_late, waits, refills;
} AudioOutAcc;

static uint32_t clamp_u32(uint64_t value) {
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static int audio_port_format(void) {
  return channels == 2 ? SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO
                       : SCE_AUDIO_OUT_PARAM_FORMAT_S16_MONO;
}

/* Adds one after-push queue occupancy sample to the PIPE/LATENCY window (audio_q_avg/audio_q_max)
 * and closes the window about once a second. Receive thread. now_us is a clock read the caller
 * already made. */
static void record_audio_occupancy_sample(uint32_t occ, uint64_t now_us) {
  audio_occ_window_sum += occ;
  audio_occ_window_count++;
  if (occ > audio_occ_window_max)
    audio_occ_window_max = occ;

  if (audio_occ_window_start_us == 0)
    audio_occ_window_start_us = now_us;
  if (now_us - audio_occ_window_start_us >= AUDIO_OCC_WINDOW_US) {
    /* Fixed-point like decode_q_occ_avg in video.c: the queue's interesting range is a few
     * frames, which plain integer averaging would hide. */
    context.stream.audio_q_occ_avg =
        (uint32_t)(((uint64_t)audio_occ_window_sum * OCC_AVG_FIXED_POINT_SCALE) /
                   audio_occ_window_count);
    context.stream.audio_q_occ_max = audio_occ_window_max;
    audio_occ_window_sum = 0;
    audio_occ_window_count = 0;
    audio_occ_window_max = 0;
    audio_occ_window_start_us = now_us;
  }
}

/* PIPE/AUDIO: one hand-off. occ is the queue occupancy after the push (and any catch-up),
 * dropped the frames the catch-up rule discarded (0 normally). Receive thread. */
static void record_audio_probe(ChiakiTakionRecvTimeAcc *probe, uint32_t occ, size_t dropped) {
  ChiakiTakionAudioStats *stats = &probe->audio_stats;
  if (dropped > 0) {
    probe->acatchup++;
    stats->catchup_frames += (uint32_t)dropped;
  }
  if (stats->q_n == 0 || occ < stats->q_min)
    stats->q_min = occ;
  if (occ > stats->q_max)
    stats->q_max = occ;
  stats->q_n++;
  stats->q_sum += occ;
}

void vita_audio_init(unsigned int channels_, unsigned int rate_, void *user) {
  (void)user;
  if (port == -1) {
    // set globals
    rate = rate_;
    channels = channels_;
    sample_bytes = channels * sizeof(int16_t);

    LOGD("VITA AUDIO :: init with %d channels at %dHz", channels, rate);
    // Note: initializing to 960 is arbitrary since we'll reset the value when we re-init
    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 960, rate, audio_port_format());
    if (port < 0) {
      LOGD("VITA AUDIO :: STARTUP ERROR 0x%x", port);
      return;
    }
    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
                         (int[]){SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB});
  }
}

/* Publishes the audio thread's window under the audioout_lock seqlock (odd while writing; the
 * reporter retries unless it saw the same even value before and after), like PIPE/DECHOLD. */
static void audio_publish_window(const AudioOutAcc *acc, uint32_t win, uint64_t wall_us) {
  AudioOutWindow out;
  out.win = win;
  out.out_calls = acc->out_calls;
  out.out_frames = acc->out_frames;
  out.aout_us = clamp_u32(acc->aout_us);
  out.aout_max_us = acc->aout_max_us;
  out.rest_n = acc->rest_n;
  out.rest_err = acc->rest_err;
  out.rest_min = acc->rest_n ? acc->rest_min : 0;
  out.rest_max = acc->rest_n ? acc->rest_max : 0;
  out.out_dry = acc->out_dry;
  out.out_gap_max_us = acc->out_gap_max_us;
  out.out_late = acc->out_late;
  out.waits = acc->waits;
  out.refills = acc->refills;
  out.wall_us = clamp_u32(wall_us);

  context.stream.audioout_lock++;
  __sync_synchronize();
  context.stream.audioout_pub = out;
  __sync_synchronize();
  context.stream.audioout_lock++;
}

/* Accounts for one finished sceAudioOutOutput call that began at start_us and returned at
 * end_us; rest is the sceAudioOutGetRestSample read taken just before it. last_end_us is when the
 * previous call returned (0 = none yet). */
static void audio_account_output(AudioOutAcc *acc, int rest, uint64_t start_us, uint64_t end_us,
                                 uint64_t last_end_us) {
  uint32_t took_us = clamp_u32(end_us - start_us);
  acc->out_calls++;
  acc->out_frames += (uint32_t)device_buffer_frames;
  acc->aout_us += took_us;
  if (took_us > acc->aout_max_us)
    acc->aout_max_us = took_us;
  if (rest < 0) {
    acc->rest_err++;  // an error return, kept out of min/max
  } else {
    if (acc->rest_n == 0 || rest < acc->rest_min)
      acc->rest_min = rest;
    if (acc->rest_n == 0 || rest > acc->rest_max)
      acc->rest_max = rest;
    acc->rest_n++;
  }
  if (rest <= 0)
    acc->out_dry++;
  if (last_end_us != 0) {
    uint64_t gap_us = start_us - last_end_us;
    uint64_t period_us = device_buffer_samples * AUDIO_US_PER_SECOND / (uint64_t)rate;
    uint32_t clamped_gap_us = clamp_u32(gap_us);
    if (clamped_gap_us > acc->out_gap_max_us)
      acc->out_gap_max_us = clamped_gap_us;
    if (gap_us > period_us)
      acc->out_late++;
  }
}

/* The audio output thread. Sets its own priority and core, then loops: wait until the queue's
 * gate allows one device buffer (the start level the first time, one device buffer once playing),
 * read the device's rest sample, copy the buffer out of the queue under the mutex, release the
 * mutex, output it. sceAudioOutOutput blocks until the hardware can take the data and that is the
 * pacing; the mutex is never held across it. Exits promptly when audio_exit is set, also while
 * holding for a refill, without draining.
 *
 * Re-arm: when the thread had to sleep for lack of a device buffer while playing, and the rest
 * sample then reads exactly 0 (the device has run empty, the gap has already happened), the queue
 * is marked dry and output is held until the start level is queued again (counted as a refill).
 * A rest above 0 (the device still has sound) or an error return outputs at once. The rest read
 * that decides this is the same one that is the statistic for the output. */
static void *audio_thread_func(void *user) {
  (void)user;
  sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, VITA_AUDIO_THREAD_PRIORITY);
  sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_2);

  AudioOutAcc acc;
  memset(&acc, 0, sizeof(acc));
  uint64_t window_start_us = 0;
  uint64_t last_end_us = 0;
  uint32_t windows_published = 0;
  bool playing = false;  // at least one output call made: a wait is then an underrun, not the start
  bool slept = false;    // slept for lack of a device buffer since the last output

  for (;;) {
    chiaki_mutex_lock(&audio_mtx);
    if (!audio_exit && !audio_queue_may_pop(&audio_queue, device_buffer_frames)) {
      if (audio_queue_count(&audio_queue) < device_buffer_frames) {
        acc.waits++;
        slept = playing;
      }
      while (!audio_exit && !audio_queue_may_pop(&audio_queue, device_buffer_frames))
        chiaki_cond_wait(&audio_cond, &audio_mtx);
    }
    bool exit_requested = audio_exit;
    chiaki_mutex_unlock(&audio_mtx);
    if (exit_requested)
      break;

    int rest = sceAudioOutGetRestSample(port);
    if (slept) {
      slept = false;
      if (rest == 0) {
        chiaki_mutex_lock(&audio_mtx);
        audio_queue_mark_dry(&audio_queue);
        chiaki_mutex_unlock(&audio_mtx);
        acc.refills++;
        continue;
      }
    }

    chiaki_mutex_lock(&audio_mtx);
    bool popped = audio_queue_pop(&audio_queue, play_buffer, device_buffer_frames);
    chiaki_mutex_unlock(&audio_mtx);
    if (!popped)  // cannot happen (only this thread pops, a catch-up keeps >= a device buffer)
      continue;

    playing = true;
    uint64_t start_us = sceKernelGetProcessTimeWide();
    sceAudioOutOutput(port, play_buffer);
    uint64_t end_us = sceKernelGetProcessTimeWide();

    audio_account_output(&acc, rest, start_us, end_us, last_end_us);
    last_end_us = end_us;
    if (window_start_us == 0)
      window_start_us = start_us;
    if (end_us - window_start_us >= AUDIO_OUT_WINDOW_US) {
      audio_publish_window(&acc, ++windows_published, end_us - window_start_us);
      memset(&acc, 0, sizeof(acc));
      window_start_us = end_us;
    }
  }
  return NULL;
}

/* Frees the buffers and destroys the mutex and condition. Only when no thread can use them: the
 * audio thread was never created or has been joined. */
static void audio_release_resources(void) {
  free(queue_storage);
  queue_storage = NULL;
  free(play_buffer);
  play_buffer = NULL;
  if (audio_sync_ready) {
    chiaki_cond_fini(&audio_cond);
    chiaki_mutex_fini(&audio_mtx);
    audio_sync_ready = false;
  }
  memset(&audio_queue, 0, sizeof(audio_queue));
}

/* Number of frames the hand-off queue may hold before the catch-up rule applies, from
 * AUDIO_CATCHUP_BOUND_MS and the real frame duration, never below keep + 1 (keep is the start
 * level). */
static size_t audio_catchup_bound_frames(size_t keep_frames) {
  size_t bound =
      (size_t)(AUDIO_CATCHUP_BOUND_MS * (uint64_t)rate / (AUDIO_MS_PER_SECOND * frame_size));
  return bound < keep_frames + 1 ? keep_frames + 1 : bound;
}

/* One-time init, on the first frame (the frame size is only known then): sizes the buffers,
 * allocates them, configures the output and starts the audio thread. On any failure everything is
 * undone, an error is logged once and audio stays off for the session. Receive thread. */
static bool audio_start(size_t samples_count) {
  audio_init_attempted = true;
  if (port < 0 || rate <= 0 || channels <= 0 || samples_count == 0) {
    LOGE("VITA AUDIO :: cannot start: port %d, rate %d, channels %d, frame samples %d", port, rate,
         channels, (int)samples_count);
    audio_disabled = true;
    return false;
  }

  frame_size = samples_count;
  frame_bytes = frame_size * sample_bytes;
  // Output blocks must be a multiple of DEVICE_FRAME_ALIGN_SAMPLES samples.
  size_t two_pow = frame_size & ((~frame_size) + 1);  // highest power of 2 dividing frame_size
  device_buffer_frames =
      two_pow >= DEVICE_FRAME_ALIGN_SAMPLES ? 1 : DEVICE_FRAME_ALIGN_SAMPLES / two_pow;
  device_buffer_samples = frame_size * device_buffer_frames;

  // Start level: a device buffer plus the refill cushion (at least one frame). It is also what a
  // catch-up keeps.
  size_t cushion_frames =
      (size_t)(AUDIO_REFILL_CUSHION_MS * (uint64_t)rate / (AUDIO_MS_PER_SECOND * frame_size));
  if (cushion_frames < 1)
    cushion_frames = 1;
  size_t start_level = device_buffer_frames + cushion_frames;
  size_t bound = audio_catchup_bound_frames(start_level);
  size_t capacity = bound + 1;
  queue_storage = malloc(capacity * frame_bytes);
  play_buffer = malloc(device_buffer_frames * frame_bytes);
  if (!queue_storage || !play_buffer) {
    LOGE("VITA AUDIO :: failed to allocate audio buffers (%d + %d bytes) - audio off",
         (int)(capacity * frame_bytes), (int)(device_buffer_frames * frame_bytes));
    goto fail;
  }
  if (!audio_queue_init(&audio_queue, queue_storage, frame_bytes, capacity, bound, start_level)) {
    LOGE("VITA AUDIO :: hand-off queue refused capacity %d bound %d start level %d - audio off",
         (int)capacity, (int)bound, (int)start_level);
    goto fail;
  }
  chiaki_mutex_init(&audio_mtx, false);
  chiaki_cond_init(&audio_cond, &audio_mtx);
  audio_sync_ready = true;
  audio_exit = false;

  sceAudioOutSetConfig(port, device_buffer_samples, rate, audio_port_format());

  ChiakiErrorCode thread_err = chiaki_thread_create(&audio_thread, audio_thread_func, NULL);
  if (thread_err != CHIAKI_ERR_SUCCESS) {
    LOGE("VITA AUDIO :: failed to create audio thread: %d - audio off", thread_err);
    goto fail;
  }
  chiaki_thread_set_name(&audio_thread, "VitaAudio");
  audio_thread_started = true;

  did_secondary_init = true;
  LOGD(
      "VITA AUDIO :: init complete: frame_size %d, device_buffer_frames %d, start level %d, "
      "catch-up bound %d frames, queue capacity %d frames",
      (int)frame_size, (int)device_buffer_frames, (int)start_level, (int)bound, (int)capacity);
  return true;

fail:
  audio_release_resources();
  audio_disabled = true;
  return false;
}

static void log_audio_session_stats(void) {
  if (audio_frames_received == 0)
    return;
  float catchup_rate = (float)audio_catchup_events / (float)audio_frames_received * 100.0f;
  LOGD("VITA AUDIO :: Session stats - Frames: %u, Catchups: %u (%.2f%%), frames dropped: %u",
       audio_frames_received, audio_catchup_events, catchup_rate, audio_catchup_dropped);
}

void vita_audio_cleanup() {
  if (!audio_init_attempted)
    return;

  // Same discipline as vita_h264_stop(): signal, JOIN, only then free and destroy.
  if (audio_thread_started) {
    chiaki_mutex_lock(&audio_mtx);
    audio_exit = true;
    chiaki_cond_signal(&audio_cond);
    chiaki_mutex_unlock(&audio_mtx);
    chiaki_thread_join(&audio_thread, NULL);
    audio_thread_started = false;
  }

  log_audio_session_stats();
  audio_release_resources();
  did_secondary_init = false;
  audio_init_attempted = false;
  audio_disabled = false;
  audio_exit = false;
  audio_frames_received = 0;
  audio_catchup_events = 0;
  audio_catchup_dropped = 0;
  audio_occ_window_start_us = 0;
  audio_occ_window_sum = 0;
  audio_occ_window_count = 0;
  audio_occ_window_max = 0;
  context.stream.audio_q_occ_avg = 0;
  context.stream.audio_q_occ_max = 0;
}

bool vita_audio_started(void) {
  return did_secondary_init;
}

/* Queues one frame for the audio thread. Never blocks beyond a short mutex hold that covers a
 * bounded copy, and never touches the sound hardware. start_us is a clock read the caller already
 * made; probe is the receive thread's accumulator (NULL if the caller is not that thread). */
static void audio_hand_off(const int16_t *buf_in, size_t samples_count,
                           ChiakiTakionRecvTimeAcc *probe, uint64_t start_us) {
  if (audio_disabled)
    return;
  if (!audio_init_attempted && !audio_start(samples_count))
    return;

  if (samples_count != frame_size) {
    LOGD("VITA AUDIO :: Expected %d (frame_size) samples but received %d.", (int)frame_size,
         (int)samples_count);
    return;
  }

  chiaki_mutex_lock(&audio_mtx);
  size_t dropped = audio_queue_push(&audio_queue, buf_in);
  size_t occ = audio_queue_count(&audio_queue);
  if (occ >= device_buffer_frames)
    chiaki_cond_signal(&audio_cond);
  chiaki_mutex_unlock(&audio_mtx);

  audio_frames_received++;
  record_audio_occupancy_sample((uint32_t)occ, start_us);
  if (probe)
    record_audio_probe(probe, (uint32_t)occ, dropped);
  if (dropped > 0) {
    audio_catchup_events++;
    audio_catchup_dropped += (uint32_t)dropped;
    LOGD("VITA AUDIO :: audio catchup: dropped %d frames, %d queued", (int)dropped, (int)occ);
  }
}

/* Receive thread. Reads the clock twice when called on the receive thread (start, end) to time
 * the whole callback for PIPE/RECVTIME; the start read also serves the occupancy window. */
void vita_audio_cb(int16_t *buf_in, size_t samples_count, void *user) {
  (void)user;
  ChiakiTakionRecvTimeAcc *probe = recv_probe_acc();
  uint64_t start_us = sceKernelGetProcessTimeWide();
  if (probe)
    probe->audio_stats.decoded++;
  audio_hand_off(buf_in, samples_count, probe, start_us);
  if (probe)
    chiaki_takion_time_bucket_add(&probe->acb, sceKernelGetProcessTimeWide() - start_us);
}
