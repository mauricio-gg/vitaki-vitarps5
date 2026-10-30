// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the mic capture thread's decision (GH #474). Run natively:
//
//   cc -std=c99 -Wall -Wextra -I vita/include test/mic_decide_tests.c vita/src/mic_decide.c \
//      -o /tmp/mic_decide_tests && /tmp/mic_decide_tests

#include <assert.h>
#include <stdio.h>

#include "mic_decide.h"

typedef struct sim_t {
  bool sending, connected;
  int opens, closes, announce_on, announce_off, connects;
} Sim;

/* One pass of the capture thread's loop: decide, then apply the action the way the thread does. */
static void pass(Sim *s, bool muted, bool stop_requested) {
  const MicDecideIn in = {muted, stop_requested, s->sending, s->connected};
  const MicDecision d = mic_decide(&in);
  if (d.action == MIC_ACTION_OPEN) {
    s->opens++;
    if (d.connect_first) {
      s->connects++;
      s->connected = true;
    }
    s->announce_on++;
    s->sending = true;
  } else if (d.action == MIC_ACTION_CLOSE) {
    s->closes++;
    if (d.announce)
      s->announce_off++;
    s->sending = false;
  }
}

/* catches: a muted mic capturing and sending audio to the PS5: the port opening or "mic on" being
 * announced at any point while muted (idle passes, a stop request), instead of only after the
 * player unmutes. */
static void test_no_open_while_muted_until_unmute(void) {
  Sim s = {0};
  pass(&s, false, false); /* live: opens and connects once */
  assert(s.sending && s.opens == 1 && s.connects == 1 && s.announce_on == 1);

  pass(&s, true, false); /* mute: closes and announces off */
  assert(!s.sending && s.closes == 1 && s.announce_off == 1);

  for (int i = 0; i < 1000; i++)
    pass(&s, true, false); /* many idle passes while muted */
  pass(&s, true, true);    /* a stop request while muted */
  pass(&s, true, true);
  assert(s.opens == 1 && s.announce_on == 1 && !s.sending);

  pass(&s, false, false); /* unmute: only now it opens, without connecting again */
  assert(s.sending && s.opens == 2 && s.connects == 1 && s.announce_on == 2);
}

/* catches: any input combination that returns OPEN while muted. */
static void test_muted_never_opens_for_any_input(void) {
  for (int bits = 0; bits < 8; bits++) {
    const MicDecideIn in = {.muted = true,
                            .stop_requested = (bits & 1) != 0,
                            .sending = (bits & 2) != 0,
                            .connected = (bits & 4) != 0};
    assert(mic_decide(&in).action != MIC_ACTION_OPEN);
  }
}

/* catches: the session being told "mic off" on a stop, when it is already going away, or a stop
 * request leaving the port open. */
static void test_stop_closes_without_announcing(void) {
  Sim s = {0};
  pass(&s, false, false);
  pass(&s, false, true);
  assert(!s.sending && s.closes == 1 && s.announce_off == 0);
  pass(&s, false, true);
  assert(s.opens == 1); /* never reopens while stopping */
}

int main(void) {
  test_no_open_while_muted_until_unmute();
  test_muted_never_opens_for_any_input();
  test_stop_closes_without_announcing();
  printf("mic_decide_tests: all passed\n");
  return 0;
}
