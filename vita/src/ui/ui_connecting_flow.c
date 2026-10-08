/**
 * @file ui_connecting_flow.c
 * @brief Pure rules for the Connecting screen (see ui_connecting_flow.h)
 */

#include "ui/ui_connecting_flow.h"

#include <stddef.h>

#define STAGE_CONNECTING (UI_CONNECTION_STAGE_CONNECTING - UI_CONNECTION_STAGE_WAKING)
#define STAGE_COUNT (UI_CONNECTION_STAGE_STARTING_STREAM - UI_CONNECTION_STAGE_WAKING + 1)

/** One row of the SPEC 3.4 stage table. */
typedef struct stage_text_t {
  const char *name;
  const char *detail;
} StageText;

static const StageText STAGE_TEXT[STAGE_COUNT] = {
    {"Waking console", "Sending wake signal"},
    {"Authenticating with PSN", "Validating account tokens"},
    {"Fetching internet consoles", "Loading remote-play capable devices"},
    {"Creating PSN session", "Creating cloud-assisted session"},
    {"Preparing Remote Play", "Negotiating session"},
    {"Punching control channel", "Establishing control tunnel"},
    {"Punching data channel", "Finalizing media tunnel"},
    {"Starting stream", "Launching video pipeline"},
};

/** The stages each flow lists, in order; a flow with fewer steps is padded with -1. */
static const int FLOW_STAGES[UI_FLOW_COUNT][UI_FLOW_MAX_STEPS] = {
    [UI_FLOW_LOCAL_READY] = {4, 7, -1, -1, -1, -1, -1},
    [UI_FLOW_LOCAL_STANDBY] = {0, 4, 7, -1, -1, -1, -1},
    [UI_FLOW_INTERNET] = {1, 2, 3, 4, 5, 6, 7},
};

static const char *const FLOW_NAMES[UI_FLOW_COUNT] = {"local-ready", "local-standby", "internet"};

static const char TITLE_WAKING[] = "Waking Console";
static const char TITLE_LOCAL[] = "Starting Remote Play";
static const char TITLE_INTERNET[] = "Starting Internet Remote Play";
static const char ROUTE_LOCAL[] = "via Local Network";
static const char ROUTE_INTERNET[] = "via Internet";

/** True when @flow is a known flow. */
static bool flow_valid(UiConnectingFlow flow) {
  return flow >= 0 && flow < UI_FLOW_COUNT;
}

UiConnectingFlow ui_connecting_flow_decide(UIConnectionStage begin_stage, bool internet) {
  if (begin_stage == UI_CONNECTION_STAGE_WAKING)
    return UI_FLOW_LOCAL_STANDBY;
  return internet ? UI_FLOW_INTERNET : UI_FLOW_LOCAL_READY;
}

int ui_connecting_flow_step_count(UiConnectingFlow flow) {
  if (!flow_valid(flow))
    return 0;
  int count = 0;
  while (count < UI_FLOW_MAX_STEPS && FLOW_STAGES[flow][count] >= 0)
    count++;
  return count;
}

int ui_connecting_flow_step_stage(UiConnectingFlow flow, int step) {
  if (step < 0 || step >= ui_connecting_flow_step_count(flow))
    return -1;
  return FLOW_STAGES[flow][step];
}

const char *ui_connecting_flow_stage_name(int stage) {
  return (stage >= 0 && stage < STAGE_COUNT) ? STAGE_TEXT[stage].name : NULL;
}

const char *ui_connecting_flow_stage_detail(int stage) {
  return (stage >= 0 && stage < STAGE_COUNT) ? STAGE_TEXT[stage].detail : NULL;
}

int ui_connecting_flow_step(UiConnectingFlow flow, UIConnectionStage stage) {
  const int number = (int)stage - (int)UI_CONNECTION_STAGE_WAKING;

  if (flow == UI_FLOW_INTERNET && number == STAGE_CONNECTING)
    return 0;
  const int count = ui_connecting_flow_step_count(flow);
  for (int i = 0; i < count; i++) {
    if (FLOW_STAGES[flow][i] == number)
      return i;
  }
  return UI_FLOW_STEP_NONE;
}

const char *ui_connecting_flow_title(UiConnectingFlow flow, int step) {
  if (flow == UI_FLOW_INTERNET)
    return TITLE_INTERNET;
  if (flow == UI_FLOW_LOCAL_STANDBY && step == 0)
    return TITLE_WAKING;
  return TITLE_LOCAL;
}

UiConnectingIcon ui_connecting_flow_icon(UiConnectingFlow flow, int step) {
  if (flow == UI_FLOW_INTERNET)
    return UI_FLOW_ICON_GLOBE;
  if (flow == UI_FLOW_LOCAL_STANDBY && step == 0)
    return UI_FLOW_ICON_MOON;
  return UI_FLOW_ICON_LAN;
}

const char *ui_connecting_flow_route(UiConnectingFlow flow) {
  return flow == UI_FLOW_INTERNET ? ROUTE_INTERNET : ROUTE_LOCAL;
}

const char *ui_connecting_flow_name(UiConnectingFlow flow) {
  return flow_valid(flow) ? FLOW_NAMES[flow] : "unknown";
}
