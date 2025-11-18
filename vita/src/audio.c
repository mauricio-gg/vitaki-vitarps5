#include <string.h> // memcpy
#include <stdlib.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>


#include "audio.h"
#include "context.h"

// The following parameters may need to be changed if audio problems crop up
//
// DEVICE_BUFFERS: number of device buffers in the intermediate buffer
// A larger value should make queue clearing and the corresponding audio chop
// less frequent.
#define DEVICE_BUFFERS 4
// DEVICE_FRAME_QUEUE_LIMIT: Send more audio to the device when there are
// samples corresponding to this many frames remaining in the device queue
#define DEVICE_FRAME_QUEUE_LIMIT 0

// Notes:
// 1. The Vita requires a sample count divisible by 64.
//    In my (ywnico) testing on a PS5, the frame size is 480 (480/64 = 7.5).
//    So we need to create an intermediate buffer of length frame_size * mult,
//    where mult is 2, 4, etc. as needed to make the length divisible by 64.
// 2. Our intermediate buffer is longer than the buffer we send to the audio
//    device. We call the latter the `device_buffer` (sorry for my poor naming
//    skills). So the intermediate buffer (`buffer`) consists of a total of
//    `DEVICE_BUFFERS` device buffers, and each of these consists of
//    mult * frame_size samples in order to be divisible by 64.
//    As we receive data from Chiaki (`buf_in`), we add it onto `buffer`. As
//    each device_buffer segment gets filled out, we send it to the audio device.
// 3. Audio is not sent to the device until it has has <=
//    DEVICE_FRAME_QUEUE_LIMIT*frame_size samples remaining (gotten through
//    sceAudioOutGetRestSample). This value, along with the DEVICE_BUFFERS
//    parameter, may need to be adjusted if there are audio problems in
//    different situations. I have tested only on decent wifi and a PS5, for
//    which DEVICE_FRAME_QUEUE_LIMIT=0 works and the audio data from chiaki
//    never gets ahead of the audio output device.

int port = -1;
int rate = -1;
int channels = -1;

// on first call to vita_audio_cb, get the actual frame size, re-init audio
// port, and set up intermediate buffer
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

size_t device_buffer_from_frame(int frame) {
    return (size_t) ( (frame + buffer_frames) % buffer_frames ) / device_buffer_frames;
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
        port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 960, rate, channels == 2 ? SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO : SCE_AUDIO_OUT_PARAM_FORMAT_S16_MONO);
           if (port < 0) {
            LOGD("VITA AUDIO :: STARTUP ERROR 0x%x", port);
            return;
        }
        sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, (int[]){SCE_AUDIO_VOLUME_0DB,SCE_AUDIO_VOLUME_0DB});
    }
}


// Determine required buffer size and allocate the required memory
void init_buffer() {
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
    write_frame_offset = 0;
    device_buffer_offset = 0;
    write_read_framediff = 0;

    LOGD("VITA AUDIO :: buffer init: buffer_frames %d, buffer_samples %d, buffer_bytes %d, frame_size %d, sample_bytes %d", buffer_frames, buffer_samples, buffer_bytes, frame_size, sample_bytes);
}

void vita_audio_cleanup() {
    if (did_secondary_init) {
        // Log final audio buffer stats
        if (audio_frames_processed > 0) {
            float catchup_rate = (float)audio_catchup_count / (float)audio_frames_processed * 100.0f;
            LOGD("VITA AUDIO :: Session stats - Frames: %lu, Catchups: %lu (%.2f%%)",
                 audio_frames_processed, audio_catchup_count, catchup_rate);
        }

        free(buffer);
        did_secondary_init = false;
        audio_catchup_count = 0;
        audio_frames_processed = 0;
    }
}

void vita_audio_cb(int16_t *buf_in, size_t samples_count, void *user) {
    if (!did_secondary_init) {
        // Set audio thread priority for low latency
        sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
        sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, SCE_KERNEL_CPU_MASK_USER_0);

        frame_size = samples_count;

        init_buffer();

        sceAudioOutSetConfig(port, device_buffer_samples, rate, channels == 2 ? SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO : SCE_AUDIO_OUT_PARAM_FORMAT_S16_MONO);

        did_secondary_init = true;
        LOGD("VITA AUDIO :: secondary init complete");
    }

    if (samples_count == frame_size) {

        // write to buffer
        memcpy(buffer + write_frame_offset*frame_size*sample_steps, buf_in, frame_size * sample_bytes);
        write_frame_offset = (write_frame_offset + 1) % buffer_frames;
        write_read_framediff++;
        audio_frames_processed++;

        if (write_read_framediff >= device_buffer_frames) {

            bool should_output = false;
            if (write_read_framediff >= buffer_frames) {
                // If the incoming buffers (write) have caught up to the audio
                // (read), just skip ahead. This should be analogous to the
                // queue clearing in the nintendo switch code.
                //
                // Specifically: if we have just written to a frame filling up
                // the device buffer behind the current device buffer offset,
                // move the device buffer offset back by one so that we output
                // the data just written. Otherwise, if we wait for the next
                // frame, we'll be stuck playing a device buffer which contains
                // one frame of new data and the rest old data.

                audio_catchup_count++;

                LOGD("VITA AUDIO :: audio catchup: [before] write_read_framediff %d, write_frame_offset %d, device_buffer_offset %d (read frame offset %d)", write_read_framediff, write_frame_offset, device_buffer_offset, device_buffer_offset*device_buffer_frames);

                device_buffer_offset = device_buffer_from_frame( ((int) write_frame_offset) - 1 );

                write_read_framediff = device_buffer_frames + write_frame_offset % device_buffer_frames;

                LOGD("VITA AUDIO :: audio catchup: [before] write_read_framediff %d, write_frame_offset %d, device_buffer_offset %d (read frame offset %d)", write_read_framediff, write_frame_offset, device_buffer_offset, device_buffer_offset*device_buffer_frames);
                should_output = true;
            } else {
                // Otherwise, tell the device to output only if it has <=
                // DEVICE_FRAME_QUEUE_LIMIT*frame_size samples remaining.
                //
                // NOTE: I'm not sure how the Vita audio out works. I think we
                // want to avoid too long of an audio queue, but this has never
                // happened in my testing. If we run into audio problems this
                // would be a good place to start debugging.
                int remaining_samples = sceAudioOutGetRestSample(port);
                if (remaining_samples <= DEVICE_FRAME_QUEUE_LIMIT*frame_size) should_output = true;
            }

            if (should_output) {
                sceAudioOutOutput(port, buffer + device_buffer_offset*device_buffer_samples*sample_steps);
                device_buffer_offset = (device_buffer_offset + 1) % DEVICE_BUFFERS;
                write_read_framediff -= device_buffer_frames;
            }
            //LOGD("VITA AUDIO :: Vita audio output write_read_framediff: %d", write_read_framediff);
        }

    } else {
        LOGD("VITA AUDIO :: Expected %d (frame_size) samples but received %d.", frame_size, samples_count);
    }

}
