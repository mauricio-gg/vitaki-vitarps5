#include <string.h> // memcpy
#include <stdlib.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>


#include "audio.h"
#include "context.h"

#define DEVICE_BUFFERS 4
#define DEVICE_FRAME_QUEUE_LIMIT 0

int port = -1;
int rate = -1;
int channels = -1;

bool did_secondary_init = false;
size_t frame_size; // # of samples in frame

int16_t* buffer;
size_t buffer_frames; // # of frames in buffer
size_t buffer_samples; // # of samples in buffer
size_t device_buffer_frames; // # of frames in device_buffer (buffer_frames / DEVICE_BUFFERS)
size_t device_buffer_samples; // # of samples in device buffer (buffer_samples / DEVICE_BUFFERS)
size_t sample_bytes; // channels * sizeof(int16_t)
size_t sample_steps; // (=channels) steps to use for array arithmetic
size_t buffer_bytes; // size of buffer in bytes = buffer_samples * sample_bytes

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

static int audio_port_format(void) {
    return channels == 2 ? SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO
                         : SCE_AUDIO_OUT_PARAM_FORMAT_S16_MONO;
}

size_t device_buffer_from_frame(int frame) {
    return (size_t) ( (frame + buffer_frames) % buffer_frames ) / device_buffer_frames;
}

static void audio_catchup_to_latest_frame(void) {
    audio_catchup_count++;
    LOGD("VITA AUDIO :: audio catchup: [before] write_read_framediff %d, write_frame_offset %d, device_buffer_offset %d (read frame offset %d)",
         write_read_framediff, write_frame_offset, device_buffer_offset, device_buffer_offset*device_buffer_frames);

    device_buffer_offset = device_buffer_from_frame(((int)write_frame_offset) - 1);
    write_read_framediff = device_buffer_frames + write_frame_offset % device_buffer_frames;

    LOGD("VITA AUDIO :: audio catchup: [after] write_read_framediff %d, write_frame_offset %d, device_buffer_offset %d (read frame offset %d)",
         write_read_framediff, write_frame_offset, device_buffer_offset, device_buffer_offset*device_buffer_frames);
}

static bool audio_should_output_now(void) {
    if (write_read_framediff >= buffer_frames) {
        audio_catchup_to_latest_frame();
        return true;
    }
    int remaining_samples = sceAudioOutGetRestSample(port);
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
        sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, (int[]){SCE_AUDIO_VOLUME_0DB,SCE_AUDIO_VOLUME_0DB});
    }
}


static void init_buffer() {
    // set global buffer_frames
    int two_pow = frame_size & ((~frame_size)+1); // highest power of 2 dividing frame_size
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
    buffer = (int16_t*)malloc(buffer_bytes);
    if (buffer == NULL) {
        LOGD("VITA AUDIO :: failed to allocate %d bytes for audio buffer", buffer_bytes);
        return;
    }
    write_frame_offset = 0;
    device_buffer_offset = 0;
    write_read_framediff = 0;

    LOGD("VITA AUDIO :: buffer init: buffer_frames %d, buffer_samples %d, buffer_bytes %d, frame_size %d, sample_bytes %d", buffer_frames, buffer_samples, buffer_bytes, frame_size, sample_bytes);
}

static void log_audio_session_stats(void) {
    if (audio_frames_processed == 0)
        return;
    float catchup_rate = (float)audio_catchup_count / (float)audio_frames_processed * 100.0f;
    LOGD("VITA AUDIO :: Session stats - Frames: %lu, Catchups: %lu (%.2f%%)",
         audio_frames_processed, audio_catchup_count, catchup_rate);
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
}

void vita_audio_cb(int16_t *buf_in, size_t samples_count, void *user) {
    if (!did_secondary_init) {
        // Set audio thread priority for low latency
        sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
        sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_0);

        frame_size = samples_count;

        init_buffer();
        if (buffer == NULL)
            return;

        sceAudioOutSetConfig(port, device_buffer_samples, rate, audio_port_format());

        did_secondary_init = true;
        LOGD("VITA AUDIO :: secondary init complete");
    }

    if (samples_count != frame_size) {
        LOGD("VITA AUDIO :: Expected %d (frame_size) samples but received %d.", frame_size, samples_count);
        return;
    }

    memcpy(buffer + write_frame_offset*frame_size*sample_steps, buf_in, frame_size * sample_bytes);
    write_frame_offset = (write_frame_offset + 1) % buffer_frames;
    write_read_framediff++;
    audio_frames_processed++;

    if (write_read_framediff < device_buffer_frames)
        return;
    if (!audio_should_output_now())
        return;

    sceAudioOutOutput(port, buffer + device_buffer_offset*device_buffer_samples*sample_steps);
    device_buffer_offset = (device_buffer_offset + 1) % DEVICE_BUFFERS;
    write_read_framediff -= device_buffer_frames;
}
