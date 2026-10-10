/**
 * @file ui_connection_stage.h
 * @brief The stages of a connect, as shown on the Connecting screen
 *
 * The single definition of UIConnectionStage. The old copies in ui.h and ui_types.h were
 * guarded by the same macro but had different values (ui_types.h had four stages, with
 * CONNECTING = 2; ui.h has the real eight, CONNECTING = 5), so a file's numbers depended on
 * which header it happened to include first.
 *
 * No SDK dependency, so the flow rules can be checked natively.
 */

#pragma once

typedef enum ui_connection_stage_t {
  UI_CONNECTION_STAGE_NONE = 0,
  UI_CONNECTION_STAGE_WAKING,
  UI_CONNECTION_STAGE_PSN_AUTH,
  UI_CONNECTION_STAGE_PSN_FETCH_DEVICES,
  UI_CONNECTION_STAGE_PSN_CREATE_SESSION,
  UI_CONNECTION_STAGE_CONNECTING,
  UI_CONNECTION_STAGE_PSN_PUNCH_CTRL,
  UI_CONNECTION_STAGE_PSN_PUNCH_DATA,
  UI_CONNECTION_STAGE_STARTING_STREAM,
} UIConnectionStage;
