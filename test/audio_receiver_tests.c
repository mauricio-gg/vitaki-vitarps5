// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) test for the lost-frame concealment in the lib audio receiver
// (lib/src/audioreceiver.c, ticket #440). It drives chiaki_audio_receiver_av_packet with recording
// sinks and checks what the sink is handed and in which order. Built and run by
// ./tools/native_tests.sh (see test/CMakeLists.txt).

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <chiaki/audioreceiver.h>
#include <chiaki/session.h>

#define UNIT_SIZE 4
#define MAX_EVENTS 64

/* What one sink was handed: a lost-frame call, or a real frame tagged by its first payload byte. */
typedef struct {
  int lost_calls;
  int events;                // entries used in order[]
  int order[MAX_EVENTS];     // -1 for a lost-frame call, else the real frame's tag
} Recorder;

static void record_frame(uint8_t *buf, size_t buf_size, void *user) {
  Recorder *rec = user;
  assert(rec->events < MAX_EVENTS);
  if (!buf && buf_size == 0) {
    rec->lost_calls++;
    rec->order[rec->events++] = -1;
  } else {
    assert(buf && buf_size == UNIT_SIZE);
    rec->order[rec->events++] = buf[0];
  }
}

typedef struct {
  ChiakiSession *session;
  ChiakiAudioReceiver receiver;
  Recorder audio, haptics;
} Fixture;

static void fixture_init(Fixture *f, ChiakiSession *session) {
  memset(f, 0, sizeof(*f));
  memset(session, 0, sizeof(*session));
  f->session = session;
  session->audio_sink.user = &f->audio;
  session->audio_sink.frame_cb = record_frame;
  session->haptics_sink.user = &f->haptics;
  session->haptics_sink.frame_cb = record_frame;
  assert(chiaki_audio_receiver_init(&f->receiver, session, NULL) == CHIAKI_ERR_SUCCESS);
}

/* One packet carrying one source unit whose first byte is the frame index's low byte. */
static void send_frame(Fixture *f, uint16_t frame_index, bool is_haptics) {
  uint8_t data[UNIT_SIZE] = {(uint8_t)frame_index};
  ChiakiTakionAVPacket packet;
  memset(&packet, 0, sizeof(packet));
  packet.codec = 5;
  packet.is_haptics = is_haptics;
  packet.frame_index = frame_index;
  packet.units_in_frame_total = 1;
  packet.units_in_frame_fec = (UNIT_SIZE << 8) | 1;
  packet.data = data;
  packet.data_size = UNIT_SIZE;
  chiaki_audio_receiver_av_packet(&f->receiver, &packet);
}

/* catches: concealment that fires on the very first frame (no baseline yet) and would play filler
 * before any real sound, or that does not fire on an ordinary gap, leaving the queue one frame
 * lower per loss (the crackle of #440). */
static void test_gap_below_cap_conceals_each_lost_frame_before_the_real_one(void) {
  ChiakiSession session;
  Fixture f;
  fixture_init(&f, &session);
  send_frame(&f, 100, false);
  assert(f.audio.lost_calls == 0 && f.audio.events == 1);
  send_frame(&f, 101, false);
  assert(f.audio.lost_calls == 0);
  send_frame(&f, 104, false); /* 102 and 103 lost */
  assert(f.audio.events == 5);
  assert(f.audio.order[2] == -1 && f.audio.order[3] == -1 && f.audio.order[4] == 104);
  assert(session.stream_connection.takion.recvtime_acc.audio_stats.frames_concealed == 2);
  assert(session.stream_connection.takion.recvtime_acc.audio_stats.frames_missing == 2);
  chiaki_audio_receiver_fini(&f.receiver);
}

/* catches: a long stall producing a flood of filler that the catch-up rule then throws away. */
static void test_gap_above_cap_conceals_only_the_cap(void) {
  ChiakiSession session;
  Fixture f;
  fixture_init(&f, &session);
  send_frame(&f, 100, false);
  send_frame(&f, 100 + 1 + 40, false); /* 40 lost */
  assert(f.audio.lost_calls == CHIAKI_AUDIO_RECEIVER_CONCEAL_MAX_FRAMES);
  assert(f.audio.order[f.audio.events - 1] == 141);
  assert(session.stream_connection.takion.recvtime_acc.audio_stats.frames_missing == 40);
  chiaki_audio_receiver_fini(&f.receiver);
}

/* catches: the 16-bit wrap read as a gap of 65535, which would conceal a full cap of filler every
 * 11 minutes of a healthy stream. A real gap across the wrap is still concealed. */
static void test_wrap_is_not_a_gap(void) {
  ChiakiSession session;
  Fixture f;
  fixture_init(&f, &session);
  /* A receiver's baseline starts at 0, so walk up to the wrap in two big (and concealed) jumps. */
  send_frame(&f, 0x7000, false);
  send_frame(&f, 0xe000, false);
  send_frame(&f, 0xfffe, false);
  int lost_before_wrap = f.audio.lost_calls;
  send_frame(&f, 0xffff, false);
  send_frame(&f, 0x0000, false);
  assert(f.audio.lost_calls == lost_before_wrap);
  send_frame(&f, 0x0002, false); /* 1 lost, just past the wrap */
  assert(f.audio.lost_calls == lost_before_wrap + 1);
  chiaki_audio_receiver_fini(&f.receiver);
}

/* catches: filler frames sent to the haptics sink, which would feed noise to the controller motors. */
static void test_haptics_are_never_concealed(void) {
  ChiakiSession session;
  Fixture f;
  fixture_init(&f, &session);
  send_frame(&f, 100, true);
  send_frame(&f, 110, true);
  assert(f.haptics.events == 2 && f.haptics.lost_calls == 0);
  assert(f.audio.events == 0);
  assert(session.stream_connection.takion.recvtime_acc.audio_stats.frames_concealed == 0);
  chiaki_audio_receiver_fini(&f.receiver);
}

int main(void) {
  test_gap_below_cap_conceals_each_lost_frame_before_the_real_one();
  test_gap_above_cap_conceals_only_the_cap();
  test_wrap_is_not_a_gap();
  test_haptics_are_never_concealed();
  printf("audio_receiver_tests: all passed\n");
  return 0;
}
