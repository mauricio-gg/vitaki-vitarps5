#include <string.h>  // memcpy
#include <stdlib.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include "audio.h"
#include "context.h"
#include "recv_probe.h"

#define DEVICE_BUFFERS 4
#define DEVICE_FRAME_QUEUE_LIMIT 0

int port = -1;
int rate = -1;
int channels = -1;

bool did_secondary_init = false;
size_t frame_size;  // # of samples in frame

int16_t *buffer;
size_t buffer_frames;          // # of frames in buffer
size_t buffer_samples;         // # of samples in buffer
size_t device_buffer_frames;   // # of frames in device_buffer (buffer_frames / DEVICE_BUFFERS)
size_t device_buffer_samples;  // # of samples in device buffer (buffer_samples / DEVICE_BUFFERS)
size_t sample_bytes;           // channels * sizeof(int16_t)
size_t sample_steps;           // (=channels) steps to use for array arithmetic
size_t buffer_bytes;           // size of buffer in bytes = buffer_samples * sample_bytes

// write_frame_offset: offset in buffer (counted in frames, not bytes or samples)
// Note that this is updated after the buffer is written to, so it represents
// the offset of the /next/ frame to be written.
size_t write_frame_offset;
// device_buffer_offset: offset in buffer (counted in device buffers, not frames)
// Note that this is updated after each audio output, so it represents the
// offset of the /next/ batch of audio to be played.
size_t device_buffer_offset;
int write_read_framediff;

// Audio buffer monitoring for detecting lag accumulation
static uint64_t audio_catchup_count = 0;
static uint64_t audio_frames_processed = 0;

/* Latency investigation (item 3): audio ring occupancy window. write_read_framediff
 * (sampled below) already IS "frames buffered but not yet handed to sceAudioOutOutput" --
 * exactly the occupancy this item asks for, no new bookkeeping needed to derive it.
 * Single-writer (audio thread only touches these, same as every other static in this
 * file); window-closed and published into context.stream.audio_q_occ_avg/max every ~1s,
 * mirroring video.c's record_incoming_frame_sample() window-close convention (D1 decode
 * timing) so both occupancy metrics read the same way in the log. */
#define AUDIO_OCC_WINDOW_US 1000000ULL
static uint64_t audio_occ_window_start_us = 0;
static uint32_t audio_occ_window_sum = 0;
static uint32_t audio_occ_window_count = 0;
static uint32_t audio_occ_window_max = 0;

/* PIPE/RECVTIME (#413): the receive thread's accumulator for the duration of one vita_audio_cb
 * call, or NULL when the caller is not the receive thread; and the clock read that ended the
 * sceAudioOutOutput call (0 if there was none), reused as the end of the callback. */
static ChiakiTakionRecvTimeAcc *audio_probe = NULL;
static uint64_t audio_output_end_us = 0;

/* PIPE/AUDIO (#413): state that lives across calls and the per-decision facts the output step
 * reads. audio_last_output_end_us is when the previous sceAudioOutOutput returned (0 = none yet);
 * audio_decision_forced / audio_decision_rest record how the current output decision was made. */
#define AUDIO_US_PER_SECOND 1000000ULL
static uint64_t audio_last_output_end_us = 0;
static bool audio_decision_forced = false;
static int audio_decision_rest = 0;

static void record_audio_occupancy_sample(void) {
  /* write_read_framediff is signed (see declaration above) and briefly negative is not
   * something this code path expects, but a bare (uint32_t) cast of a negative value
   * would wrap to ~4.29e9 and permanently latch audio_occ_window_max there with no way
   * to distinguish it from real data. Guard defensively — code review fix. */
  uint32_t occ = write_read_framediff > 0 ? (uint32_t)write_read_framediff : 0u;
  audio_occ_window_sum += occ;
  audio_occ_window_count++;
  if (occ > audio_occ_window_max)
    audio_occ_window_max = occ;

  uint64_t now_us = sceKernelGetProcessTimeWide();
  if (audio_occ_window_start_us == 0)
    audio_occ_window_start_us = now_us;
  if (now_us - audio_occ_window_start_us >= AUDIO_OCC_WINDOW_US) {
    /* Code review fix: same fixed-point rationale as decode_q_occ_avg in video.c -- the
     * 8-frame audio buffer's interesting range is small enough that plain integer
     * averaging would hide a sub-frame occupancy shift. audio_occ_window_sum is bounded
     * by ~frames-per-second (tens to low hundreds), so the x100 multiply is nowhere near
     * uint32_t overflow, but do it in the same 64-bit pattern as video.c for consistency
     * and to stay correct if this window's sample count ever grows. */
    context.stream.audio_q_occ_avg =
        audio_occ_window_count > 0
            ? (uint32_t)(((uint64_t)audio_occ_window_sum * OCC_AVG_FIXED_POINT_SCALE) /
                         audio_occ_window_count)
            : 0;
    context.stream.audio_q_occ_max = audio_occ_window_max;
    audio_occ_window_sum = 0;
    audio_occ_window_count = 0;
    audio_occ_window_max = 0;
    audio_occ_window_start_us = now_us;
  }
}

static int audio_port_format(void) {
  return channels == 2 ? SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO
                       : SCE_AUDIO_OUT_PARAM_FORMAT_S16_MONO;
}

size_t device_buffer_from_frame(int frame) {
  return (size_t)((frame + buffer_frames) % buffer_frames) / device_buffer_frames;
}

static void audio_catchup_to_latest_frame(void) {
  audio_catchup_count++;
  if (audio_probe)
    audio_probe->acatchup++;
  LOGD(
      "VITA AUDIO :: audio catchup: [before] write_read_framediff %d, write_frame_offset %d, "
      "device_buffer_offset %d (read frame offset %d)",
      write_read_framediff, write_frame_offset, device_buffer_offset,
      device_buffer_offset * device_buffer_frames);

  int framediff_before = write_read_framediff;
  device_buffer_offset = device_buffer_from_frame(((int)write_frame_offset) - 1);
  write_read_framediff = device_buffer_frames + write_frame_offset % device_buffer_frames;
  if (audio_probe && framediff_before > write_read_framediff)
    audio_probe->audio_stats.catchup_frames += (uint32_t)(framediff_before - write_read_framediff);

  LOGD(
      "VITA AUDIO :: audio catchup: [after] write_read_framediff %d, write_frame_offset %d, "
      "device_buffer_offset %d (read frame offset %d)",
      write_read_framediff, write_frame_offset, device_buffer_offset,
      device_buffer_offset * device_buffer_frames);
}

static bool audio_should_output_now(void) {
  audio_decision_forced = false;
  if (write_read_framediff >= buffer_frames) {
    audio_catchup_to_latest_frame();
    audio_decision_forced = true;
    return true;
  }
  int remaining_samples = sceAudioOutGetRestSample(port);
  audio_decision_rest = remaining_samples;
  if (audio_probe) {
    ChiakiTakionAudioStats *stats = &audio_probe->audio_stats;
    if (remaining_samples < 0) {
      stats->rest_err++;  // an error return, kept out of min/max
    } else {
      if (stats->rest_n == 0 || remaining_samples < stats->rest_min)
        stats->rest_min = remaining_samples;
      if (stats->rest_n == 0 || remaining_samples > stats->rest_max)
        stats->rest_max = remaining_samples;
      stats->rest_n++;
    }
  }
  return remaining_samples <= DEVICE_FRAME_QUEUE_LIMIT * frame_size;
}

void vita_audio_init(unsigned int channels_, unsigned int rate_, void *user) {
  if (port == -1) {
    // set globals
    rate = rate_;
    channels = channels_;
    sample_steps = channels;
    sample_bytes = sample_steps * sizeof(int16_t);

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

static void init_buffer() {
  // set global buffer_frames
  int two_pow = frame_size & ((~frame_size) + 1);  // highest power of 2 dividing frame_size
  if (two_pow >= 64) {
    // frame_size already divisible by 64
    device_buffer_frames = 1;
  } else {
    device_buffer_frames = 64 / two_pow;
  }

  // set globals
  buffer_frames = device_buffer_frames * DEVICE_BUFFERS;
  buffer_samples = frame_size * buffer_frames;
  buffer_bytes = buffer_samples * sample_bytes;

  device_buffer_samples = frame_size * device_buffer_frames;

  // initialize buffer
  buffer = (int16_t *)malloc(buffer_bytes);
  if (buffer == NULL) {
    LOGD("VITA AUDIO :: failed to allocate %d bytes for audio buffer", buffer_bytes);
    return;
  }
  write_frame_offset = 0;
  device_buffer_offset = 0;
  write_read_framediff = 0;

  LOGD(
      "VITA AUDIO :: buffer init: buffer_frames %d, buffer_samples %d, buffer_bytes %d, frame_size "
      "%d, sample_bytes %d",
      buffer_frames, buffer_samples, buffer_bytes, frame_size, sample_bytes);
}

static void log_audio_session_stats(void) {
  if (audio_frames_processed == 0)
    return;
  float catchup_rate = (float)audio_catchup_count / (float)audio_frames_processed * 100.0f;
  LOGD("VITA AUDIO :: Session stats - Frames: %lu, Catchups: %lu (%.2f%%)", audio_frames_processed,
       audio_catchup_count, catchup_rate);
}

void vita_audio_cleanup() {
  if (!did_secondary_init)
    return;

  log_audio_session_stats();
  free(buffer);
  buffer = NULL;
  did_secondary_init = false;
  audio_catchup_count = 0;
  audio_frames_processed = 0;
  audio_last_output_end_us = 0;
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

/* PIPE/AUDIO: one sceAudioOutOutput call just made (it began at output_start_us and returned at
 * audio_output_end_us). Reuses the two clock reads the call is already timed with. */
static void record_audio_output(ChiakiTakionRecvTimeAcc *probe, uint64_t output_start_us) {
  ChiakiTakionAudioStats *stats = &probe->audio_stats;
  stats->out_calls++;
  stats->out_frames += (uint32_t)device_buffer_frames;
  if (audio_decision_forced)
    stats->out_forced++;
  else if (audio_decision_rest <= 0)
    stats->out_dry++;
  if (audio_last_output_end_us != 0 && rate > 0) {
    uint64_t gap_us = output_start_us - audio_last_output_end_us;
    uint64_t period_us = device_buffer_samples * AUDIO_US_PER_SECOND / (uint64_t)rate;
    if (gap_us > UINT32_MAX)
      gap_us = UINT32_MAX;
    if (gap_us > stats->out_gap_max_us)
      stats->out_gap_max_us = (uint32_t)gap_us;
    if (gap_us > period_us)
      stats->out_late++;
  }
  audio_last_output_end_us = audio_output_end_us;
}

static void audio_cb_body(int16_t *buf_in, size_t samples_count) {
  if (!did_secondary_init) {
    // Set audio thread priority for low latency
    sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, VITA_AUDIO_THREAD_PRIORITY);
    sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_2);

    frame_size = samples_count;

    init_buffer();
    if (buffer == NULL)
      return;

    sceAudioOutSetConfig(port, device_buffer_samples, rate, audio_port_format());

    did_secondary_init = true;
    LOGD("VITA AUDIO :: secondary init complete");
  }

  if (samples_count != frame_size) {
    LOGD("VITA AUDIO :: Expected %d (frame_size) samples but received %d.", frame_size,
         samples_count);
    return;
  }

  memcpy(buffer + write_frame_offset * frame_size * sample_steps, buf_in,
         frame_size * sample_bytes);
  write_frame_offset = (write_frame_offset + 1) % buffer_frames;
  write_read_framediff++;
  audio_frames_processed++;
  record_audio_occupancy_sample();

  if (write_read_framediff < device_buffer_frames) {
    if (audio_probe)
      audio_probe->ashort++;
    return;
  }
  if (!audio_should_output_now()) {
    if (audio_probe)
      audio_probe->askip++;
    return;
  }

  uint64_t output_start_us = audio_probe ? sceKernelGetProcessTimeWide() : 0;
  sceAudioOutOutput(port, buffer + device_buffer_offset * device_buffer_samples * sample_steps);
  if (audio_probe) {
    audio_output_end_us = sceKernelGetProcessTimeWide();
    chiaki_takion_time_bucket_add(&audio_probe->aout, audio_output_end_us - output_start_us);
    record_audio_output(audio_probe, output_start_us);
  }
  device_buffer_offset = (device_buffer_offset + 1) % DEVICE_BUFFERS;
  write_read_framediff -= device_buffer_frames;
}

/* Times the whole callback for PIPE/RECVTIME. On the output path the callback is taken to end when
 * sceAudioOutOutput returns (the three statements after it are not included), which saves a clock
 * read: 3 per audio frame on that path, 2 on the others. */
void vita_audio_cb(int16_t *buf_in, size_t samples_count, void *user) {
  (void)user;
  audio_probe = recv_probe_acc();
  if (!audio_probe) {
    audio_cb_body(buf_in, samples_count);
    return;
  }
  uint64_t start_us = sceKernelGetProcessTimeWide();
  audio_output_end_us = 0;
  audio_probe->audio_stats.decoded++;
  audio_cb_body(buf_in, samples_count);
  uint64_t end_us = audio_output_end_us ? audio_output_end_us : sceKernelGetProcessTimeWide();
  chiaki_takion_time_bucket_add(&audio_probe->acb, end_us - start_us);
  audio_probe = NULL;
}
