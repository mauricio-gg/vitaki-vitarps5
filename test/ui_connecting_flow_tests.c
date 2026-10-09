// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Native (non-Vita) tests for the Connecting screen rules in vita/src/ui/ui_connecting_flow.c
// (issues #301, #341): which flow a connect is, which steps it lists, which step is current,
// and the title and route. `./tools/build.sh test` only cross-compiles, so run these on the host:
//   cc -std=c99 -Wall -Wextra -I vita/include test/ui_connecting_flow_tests.c \
//      vita/src/ui/ui_connecting_flow.c -o /tmp/ui_connecting_flow_tests && \
//      /tmp/ui_connecting_flow_tests

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui_connecting_flow.h"

/** Stage numbers a flow lists, in order, as one string such as "0,4,7". */
static void flow_stage_list(UiConnectingFlow flow, char *out, size_t size) {
  out[0] = '\0';
  for (int i = 0; i < ui_connecting_flow_step_count(flow); i++) {
    char one[8];
    snprintf(one, sizeof(one), "%s%d", i ? "," : "", ui_connecting_flow_step_stage(flow, i));
    strncat(out, one, size - strlen(out) - 1);
  }
}

/** A wrong list shows steps the connect never goes through, or hides ones it does. */
static void test_flows_list_the_spec_stages(void) {
  char list[32];
  flow_stage_list(UI_FLOW_LOCAL_READY, list, sizeof(list));
  assert(strcmp(list, "4,7") == 0);
  flow_stage_list(UI_FLOW_LOCAL_STANDBY, list, sizeof(list));
  assert(strcmp(list, "0,4,7") == 0);
  flow_stage_list(UI_FLOW_INTERNET, list, sizeof(list));
  assert(strcmp(list, "1,2,3,4,5,6,7") == 0);
  flow_stage_list(UI_FLOW_INTERNET_STANDBY, list, sizeof(list));
  assert(strcmp(list, "0,1,3,4,5,6,7") == 0);
}

/** A begin at WAKING is a standby console and the route still decides: Internet to a console
 * that must be woken is its own flow, not the local one (#341). */
static void test_flow_is_decided_from_the_begin_stage_and_route(void) {
  assert(ui_connecting_flow_decide(UI_CONNECTION_STAGE_WAKING, false) == UI_FLOW_LOCAL_STANDBY);
  assert(ui_connecting_flow_decide(UI_CONNECTION_STAGE_WAKING, true) == UI_FLOW_INTERNET_STANDBY);
  assert(ui_connecting_flow_decide(UI_CONNECTION_STAGE_CONNECTING, false) == UI_FLOW_LOCAL_READY);
  assert(ui_connecting_flow_decide(UI_CONNECTION_STAGE_CONNECTING, true) == UI_FLOW_INTERNET);
}

/** The old screen flipped its title per stage: an Internet connect read "Starting Remote Play"
 * until host.c reached the PSN stages. The title may change only on a standby flow's first step. */
static void test_title_is_per_flow_not_per_stage(void) {
  for (int step = 0; step < UI_FLOW_MAX_STEPS; step++) {
    assert(strcmp(ui_connecting_flow_title(UI_FLOW_INTERNET, step),
                  "Starting Internet Remote Play") == 0);
    assert(ui_connecting_flow_icon(UI_FLOW_INTERNET, step) == UI_FLOW_ICON_GLOBE);
    assert(strcmp(ui_connecting_flow_title(UI_FLOW_LOCAL_READY, step), "Starting Remote Play") ==
           0);
    assert(ui_connecting_flow_icon(UI_FLOW_LOCAL_READY, step) == UI_FLOW_ICON_LAN);
  }
  assert(strcmp(ui_connecting_flow_title(UI_FLOW_LOCAL_STANDBY, 0), "Waking Console") == 0);
  assert(ui_connecting_flow_icon(UI_FLOW_LOCAL_STANDBY, 0) == UI_FLOW_ICON_MOON);
  assert(strcmp(ui_connecting_flow_title(UI_FLOW_LOCAL_STANDBY, 1), "Starting Remote Play") == 0);
  assert(strcmp(ui_connecting_flow_title(UI_FLOW_LOCAL_STANDBY, 2), "Starting Remote Play") == 0);
  assert(strcmp(ui_connecting_flow_route(UI_FLOW_INTERNET), "via Internet") == 0);
  assert(strcmp(ui_connecting_flow_route(UI_FLOW_LOCAL_READY), "via Local Network") == 0);
}

/** #341: an Internet connect to a console in rest mode read "via Local Network" and the local
 * icon. Through decide(), the wake step reads "Waking Console" with the globe (not the moon),
 * every later step reads the Internet title with the globe, and the route is Internet throughout
 * (CEO ruling 2026-10-09). */
static void test_internet_connect_to_a_standby_console_wakes_then_reads_internet(void) {
  const UiConnectingFlow flow = ui_connecting_flow_decide(UI_CONNECTION_STAGE_WAKING, true);
  assert(strcmp(ui_connecting_flow_title(flow, 0), "Waking Console") == 0);
  for (int step = 1; step < ui_connecting_flow_step_count(flow); step++)
    assert(strcmp(ui_connecting_flow_title(flow, step), "Starting Internet Remote Play") == 0);
  for (int step = 0; step < ui_connecting_flow_step_count(flow); step++) {
    assert(ui_connecting_flow_icon(flow, step) == UI_FLOW_ICON_GLOBE);
    assert(strcmp(ui_connecting_flow_route(flow), "via Internet") == 0);
  }
  assert(
      strcmp(ui_connecting_flow_route(ui_connecting_flow_decide(UI_CONNECTION_STAGE_WAKING, false)),
             "via Local Network") == 0);
}

/** The current step follows the stage; a stage outside the flow is reported, never indexed. */
static void test_current_step_follows_the_stage(void) {
  /* The stages host.c really reports on an Internet connect: it begins at CONNECTING, then
   * PSN_AUTH, CREATE_SESSION, PUNCH_CTRL, STARTING_STREAM. It must not start on step 3. */
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_CONNECTING) == 0);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_PSN_AUTH) == 0);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_PSN_CREATE_SESSION) == 2);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_PSN_PUNCH_CTRL) == 4);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_STARTING_STREAM) == 6);

  /* Internet to a console being woken: WAKING, then ui_screen_draw_waking() sets CONNECTING when
   * it is up, which must land on the PSN auth step (1) and not fall back to the wake step. */
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY, UI_CONNECTION_STAGE_WAKING) == 0);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY, UI_CONNECTION_STAGE_CONNECTING) == 1);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY, UI_CONNECTION_STAGE_PSN_AUTH) == 1);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY,
                                 UI_CONNECTION_STAGE_PSN_CREATE_SESSION) == 2);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY, UI_CONNECTION_STAGE_PSN_PUNCH_CTRL) ==
         4);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY, UI_CONNECTION_STAGE_STARTING_STREAM) ==
         6);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET_STANDBY, UI_CONNECTION_STAGE_PSN_FETCH_DEVICES) ==
         UI_FLOW_STEP_NONE);

  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_READY, UI_CONNECTION_STAGE_CONNECTING) == 0);
  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_READY, UI_CONNECTION_STAGE_STARTING_STREAM) == 1);
  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_STANDBY, UI_CONNECTION_STAGE_WAKING) == 0);
  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_STANDBY, UI_CONNECTION_STAGE_CONNECTING) == 1);
  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_STANDBY, UI_CONNECTION_STAGE_STARTING_STREAM) == 2);

  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_READY, UI_CONNECTION_STAGE_PSN_AUTH) ==
         UI_FLOW_STEP_NONE);
  assert(ui_connecting_flow_step(UI_FLOW_LOCAL_STANDBY, UI_CONNECTION_STAGE_PSN_PUNCH_CTRL) ==
         UI_FLOW_STEP_NONE);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_WAKING) ==
         UI_FLOW_STEP_NONE);
  assert(ui_connecting_flow_step(UI_FLOW_INTERNET, UI_CONNECTION_STAGE_NONE) == UI_FLOW_STEP_NONE);
}

int main(void) {
  test_flows_list_the_spec_stages();
  test_flow_is_decided_from_the_begin_stage_and_route();
  test_title_is_per_flow_not_per_stage();
  test_internet_connect_to_a_standby_console_wakes_then_reads_internet();
  test_current_step_follows_the_stage();
  printf("ui_connecting_flow_tests: all passed\n");
  return 0;
}
