/**
 * @file ui_connecting_flow.h
 * @brief Pure rules for the Connecting screen: which flow a connect is, which steps it lists,
 *        which step is current, and the title and icon (SPEC.md section 3.4)
 *
 * No Vita dependency, so the rules can be checked natively (test/ui_connecting_flow_tests.c).
 * A stage number is UIConnectionStage - UI_CONNECTION_STAGE_WAKING, the "#" column of the
 * SPEC stage table (0 = waking console .. 7 = starting stream).
 */

#pragma once

#include <stdbool.h>

#include "ui/ui_connection_stage.h"

/** The three flows of SPEC 3.4. The title and the listed steps follow the flow, never the stage. */
typedef enum ui_connecting_flow_t {
  UI_FLOW_LOCAL_READY = 0,  ///< stages 4, 7
  UI_FLOW_LOCAL_STANDBY,    ///< stages 0, 4, 7
  UI_FLOW_INTERNET,         ///< stages 1 to 7
  UI_FLOW_COUNT
} UiConnectingFlow;

/** The 32 px icon shown in the title row. */
typedef enum ui_connecting_icon_t {
  UI_FLOW_ICON_LAN = 0,
  UI_FLOW_ICON_GLOBE,
  UI_FLOW_ICON_MOON,
} UiConnectingIcon;

/** Most steps any flow lists (the Internet flow). */
#define UI_FLOW_MAX_STEPS 7

/** Returned by ui_connecting_flow_step() for a stage the flow does not list. */
#define UI_FLOW_STEP_NONE (-1)

/**
 * ui_connecting_flow_decide() - The flow of a connect, decided once when it begins.
 * @begin_stage: Stage the connect begins at: WAKING means a console in standby, anything else
 *               a console that is ready.
 * @internet:    The connect goes through PSN: the host's source is PSN remote, or the one-shot
 *               force flag is set (the rule host_stream() uses).
 */
UiConnectingFlow ui_connecting_flow_decide(UIConnectionStage begin_stage, bool internet);

/** ui_connecting_flow_step_count() - Number of steps @flow lists. */
int ui_connecting_flow_step_count(UiConnectingFlow flow);

/** ui_connecting_flow_step_stage() - Stage number (0..7) of step @step of @flow; -1 out of range.
 */
int ui_connecting_flow_step_stage(UiConnectingFlow flow, int step);

/** ui_connecting_flow_stage_name() - Step label for stage number @stage (0..7), or NULL. */
const char *ui_connecting_flow_stage_name(int stage);

/** ui_connecting_flow_stage_detail() - Step detail line for stage number @stage, or NULL. */
const char *ui_connecting_flow_stage_detail(int stage);

/**
 * ui_connecting_flow_step() - The step that is current while the connect is at @stage.
 *
 * Every step before it reads as done, which is how a stage host.c never reports (it sets
 * neither FETCH_DEVICES nor PUNCH_DATA) is still shown as passed. A connect begins at
 * CONNECTING even on the Internet flow, before host.c reports PSN_AUTH, so there CONNECTING
 * stands for the first step; host.c never reports CONNECTING again on that flow.
 *
 * @return the step index, or UI_FLOW_STEP_NONE when @stage is not one of the flow's stages
 */
int ui_connecting_flow_step(UiConnectingFlow flow, UIConnectionStage stage);

/** ui_connecting_flow_title() - Page title: per flow, flipping only on standby's first step. */
const char *ui_connecting_flow_title(UiConnectingFlow flow, int step);

/** ui_connecting_flow_icon() - Title icon: globe for Internet, moon on standby's first step. */
UiConnectingIcon ui_connecting_flow_icon(UiConnectingFlow flow, int step);

/** ui_connecting_flow_route() - "via Internet" or "via Local Network". */
const char *ui_connecting_flow_route(UiConnectingFlow flow);

/** ui_connecting_flow_name() - Flow name for log lines. */
const char *ui_connecting_flow_name(UiConnectingFlow flow);
