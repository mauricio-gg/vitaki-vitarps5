/**
 * @file ui_connecting.c
 * @brief The Connecting / Waking screen (SPEC.md section 3.4)
 */

#include "ui/ui_connecting.h"

#include <stdio.h>
#include <string.h>

#include <vita2d.h>

#include "context.h"
#include "host.h"
#include "ui/ui_animation.h"
#include "ui/ui_chrome_layout.h"
#include "ui/ui_console_status.h"
#include "ui/ui_connecting_flow.h"
#include "ui/ui_connecting_ring.h"
#include "ui/ui_hint_row.h"
#include "ui/ui_input.h"
#include "ui/ui_internal.h"
#include "ui/ui_page_frame.h"
#include "ui/ui_room_icons.h"
#include "ui/ui_spinner.h"
#include "ui/ui_state.h"
#include "ui/ui_steps.h"
#include "ui/ui_text.h"
#include "ui/ui_text_button.h"
#include "ui/ui_theme.h"
#include "ui/ui_top_bar.h"
#include "ui/ui_type_logo.h"

/** Centre of the art box (halo, spinner and ring are centred on it). */
#define ART_CX (UI_CONN_ART_X + UI_CONN_ART_W / 2)
#define ART_CY (UI_CONN_ART_Y + UI_CONN_ART_H / 2)
/** The console name sits UI_S1 under the logo, the route right under the name. */
#define NAME_Y (UI_CONN_LOGO_Y + UI_CONN_LOGO_H + UI_S1)
#define ROUTE_Y (NAME_Y + UI_T28_LINE)

/** Longest console name kept (display_name is shorter); the name is ellipsized to the art box. */
#define NAME_MAX 96

static UiTextButton s_cancel;
static UiHintLayout s_hints;
static const char CANCEL_LABEL[] = "Cancel";

/* Per-connect state, reset when a new connect begins (ui_connection_get_serial). */
static uint32_t s_serial;
static bool s_serial_valid = false;
static int s_step;
static int s_warned_stage;

/* Console name, ellipsized and measured once per change, never per frame. */
static char s_name_source[NAME_MAX];
static char s_name[NAME_MAX];
static int s_name_w;

/* Route width, measured when the flow changes. */
static UiConnectingFlow s_route_flow = UI_FLOW_COUNT;
static int s_route_w;

void ui_connecting_init(void) {
  ui_connecting_ring_init();
  ui_spinner_init();
  ui_page_frame_init();
  ui_text_button_init(&s_cancel, CANCEL_LABEL,
                      UI_CONTENT_RIGHT - ui_text_button_width(CANCEL_LABEL), UI_CONN_CANCEL_Y);
  s_cancel.focused = true;
  s_hints.count = 0;
}

/** The page-frame icon for a flow's title icon. */
static UiPageIcon page_icon(UiConnectingIcon icon) {
  switch (icon) {
    case UI_FLOW_ICON_GLOBE:
      return UI_PAGE_ICON_GLOBE;
    case UI_FLOW_ICON_MOON:
      return UI_PAGE_ICON_MOON;
    default:
      return UI_PAGE_ICON_LAN;
  }
}

/** Width function for ui_ellipsize_to_fit(): the console name's face. */
static int measure_name(const char *text, void *ctx) {
  (void)ctx;
  return ui_text_face_width(UI_FACE_T28, text);
}

/** The active console's display name, or "" when it has none. */
static const char *host_name(const VitaChiakiHost *host) {
  if (!host)
    return "";
  return host->display_name[0] ? host->display_name : host->hostname;
}

/** Refresh the cached name and its width when the console changed. */
static void update_name(const char *name) {
  if (strcmp(name, s_name_source) == 0)
    return;
  snprintf(s_name_source, sizeof(s_name_source), "%s", name);
  ui_ellipsize_to_fit(name, UI_CONN_ART_W, measure_name, NULL, s_name, sizeof(s_name));
  s_name_w = ui_text_face_width(UI_FACE_T28, s_name);
}

/**
 * current_step() - The step to show for the stage the connect is at.
 *
 * A stage the flow does not list (host.c reporting PSN stages for a standby wake that goes out
 * over the Internet, say) is logged once and the screen keeps the step it had.
 */
static int current_step(UiConnectingFlow flow) {
  const UIConnectionStage stage = ui_connection_stage();
  const uint32_t serial = ui_connection_get_serial();
  if (!s_serial_valid || serial != s_serial) {
    s_serial = serial;
    s_serial_valid = true;
    s_step = 0;
    s_warned_stage = -1;
  }

  const int step = ui_connecting_flow_step(flow, stage);
  if (step != UI_FLOW_STEP_NONE) {
    s_step = step;
  } else if ((int)stage != s_warned_stage) {
    s_warned_stage = (int)stage;
    CHIAKI_LOGW(&(context.log),
                "Connecting screen: stage %d is not in the %s flow, keeping step %d", (int)stage,
                ui_connecting_flow_name(flow), s_step);
  }
  return s_step;
}

/** Fill @out with the flow's steps; returns how many. */
static int build_steps(UiConnectingFlow flow, UiStep out[UI_STEPS_MAX]) {
  const int count = ui_connecting_flow_step_count(flow);
  for (int i = 0; i < count; i++) {
    const int stage = ui_connecting_flow_step_stage(flow, i);
    out[i] = (UiStep){ui_connecting_flow_stage_name(stage), ui_connecting_flow_stage_detail(stage)};
  }
  return count;
}

/** Draw the console's type logo, name and route centred under the art. */
static void draw_who(const VitaChiakiHost *host, UiConnectingFlow flow) {
  if (flow != s_route_flow) {
    s_route_flow = flow;
    s_route_w = ui_text_face_width(UI_FACE_T16, ui_connecting_flow_route(flow));
  }

  if (host) {
    const bool ps5 = chiaki_target_is_ps5(host->target);
    const UiTypeLogo kind = ps5 ? UI_TYPE_LOGO_PS5 : UI_TYPE_LOGO_PS4;
    const int logo_w = ui_type_logo_width(kind, UI_CONN_LOGO_H);
    ui_type_logo_draw(ps5 ? ps5_logo : img_ps4, kind, ART_CX - logo_w / 2, UI_CONN_LOGO_Y,
                      UI_CONN_LOGO_H, UI_TEXT);
  }
  if (s_name[0]) {
    ui_text_draw_face_centered_v(UI_FACE_T28, ART_CX - s_name_w / 2, NAME_Y, UI_T28_LINE, UI_TEXT,
                                 s_name);
  }
  ui_text_draw_face_centered_v(UI_FACE_T16, ART_CX - s_route_w / 2, ROUTE_Y, UI_T16_LINE, UI_TEXT_2,
                               ui_connecting_flow_route(flow));
}

/**
 * ring_state() - The ring colour for the console being connected: WARN while a Retrying
 * status message is live, ERR while an Error one is, OK otherwise.
 */
static UiRingState ring_state(const VitaChiakiHost *host) {
  if (!host)
    return UI_RING_STATE_OK;
  switch (ui_console_message_class(host->status_hint, host->status_hint_is_error,
                                   host->status_hint_expire_us, ui_anim_now_us())) {
    case UI_CONSOLE_MESSAGE_RETRYING:
      return UI_RING_STATE_WARN;
    case UI_CONSOLE_MESSAGE_ERROR:
      return UI_RING_STATE_ERR;
    default:
      return UI_RING_STATE_OK;
  }
}

bool ui_connecting_frame(void) {
  /* A tapped hint acts as that button pressed and released in one frame. */
  UiInput in = *ui_input_snapshot();
  const uint32_t tapped = ui_hint_row_tap(&s_hints, &in);
  if (tapped) {
    in.pressed |= tapped;
    in.touch.pressed = false;
    in.touch.released = false;
    in.touch.down = false;
  }
  const bool cancel =
      ui_text_button_input(&s_cancel, &in) == UI_EVENT_ACTIVATED || (in.pressed & UI_BTN_CANCEL);

  const VitaChiakiHost *host = context.active_host;
  const UiConnectingFlow flow = ui_connection_get_flow();
  const int step = current_step(flow);
  update_name(host_name(host));

  UiStep steps[UI_STEPS_MAX];
  const int step_count = build_steps(flow, steps);

  ui_page_frame_draw(page_icon(ui_connecting_flow_icon(flow, step)),
                     ui_connecting_flow_title(flow, step));
  ui_top_bar_draw(NULL);
  ui_draw_connecting_ring(ART_CX - UI_RING_SIZE / 2, ART_CY - UI_RING_SIZE / 2, UI_RING_SIZE,
                          ui_room_icons_get(ui_room_icon_for_host(host), ROOM_ICON_SIZE_RING),
                          ring_state(host));
  ui_spinner_draw(UI_SPINNER_LARGE, ART_CX, ART_CY);
  draw_who(host, flow);
  ui_draw_steps(UI_STEPS_X, UI_STEPS_Y, steps, step_count, step);
  ui_text_button_draw(&s_cancel);

  const UiHintItem hint = {.action = UI_BTN_CANCEL, .label = CANCEL_LABEL};
  ui_hint_row_layout(&s_hints, &hint, 1);
  ui_hint_row_draw(&s_hints);

  return cancel;
}
