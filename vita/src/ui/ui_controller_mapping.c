/**
 * @file ui_controller_mapping.c
 * @brief The Controller page's mapping popup (see ui_controller_mapping.h)
 */

#include "ui/ui_controller_mapping.h"

#include <stdio.h>
#include <string.h>

#include "context.h"
#include "ui/ui_controller_model.h"
#include "ui/ui_controller_zones.h"
#include "ui/ui_list_popup.h"

/* Copy (SPEC section 5) */
static const char TITLE_SHOULDER[] = "Shoulder Mapping";
static const char TITLE_FRONT[] = "Front Touch Mapping";
static const char TITLE_REAR[] = "Rear Touch Mapping";
static const char SUBTITLE_VALUE_FORMAT[] = "%s \xC2\xB7 %s";
static const char SUBTITLE_MANY_FORMAT[] = "%d Zones Selected";
static const char SUBTITLE_ONE_FORMAT[] = "%s %s";
static const char NAME_FULL_FRONT[] = "Full Front Touch";
static const char NAME_FULL_REAR[] = "Full Rear Touch";
static const char NAME_FRONT[] = "Front";
static const char NAME_REAR[] = "Rear";
static const char NAME_MIXED[] = "Mixed";
static const char HINT_ASSIGN[] = "Assign";
static const char HINT_CANCEL[] = "Cancel";

#define SUBTITLE_MAX 64

/** What the popup assigns to. */
typedef enum target_kind_t {
  TARGET_SHOULDER = 0,
  TARGET_ZONES,
  TARGET_SIDE,
} TargetKind;

typedef struct target_t {
  TargetKind kind;
  VitakiCtrlIn input;  ///< TARGET_SHOULDER
  UiCtrlSide side;     ///< TARGET_ZONES, TARGET_SIDE
  int zones[UI_CTRL_ZONES];
  int zone_count;
} Target;

static UiListPopup s_popup;
static Target s_target;

/** The name of a value for a subtitle: the output's name, or "Mixed". */
static const char *value_name(int output) {
  return output == UI_CTRL_MIXED ? NAME_MIXED : controller_output_name((VitakiCtrlOut)output);
}

/** Open the popup with @title and @subtitle, ticking and focusing the row of @current (an output,
 * or UI_CTRL_MIXED or an output the popup does not offer for none). */
static void open_popup(const char *title, const char *subtitle, int current) {
  const int current_row =
      current == UI_CTRL_MIXED ? -1 : ui_controller_model_output_row((VitakiCtrlOut)current);

  UiListRow rows[UI_CTRL_OUTPUT_COUNT] = {0};
  for (int i = 0; i < UI_CTRL_OUTPUT_COUNT; i++) {
    snprintf(rows[i].label, sizeof(rows[i].label), "%s",
             controller_output_name(ui_controller_model_output_choice(i)));
    rows[i].current = i == current_row;
  }
  ui_list_popup_open(&s_popup, &(UiListPopupSpec){
                                   .size = UI_POPUP_SIZE_L,
                                   .title = title,
                                   .subtitle = subtitle,
                                   .confirm_label = HINT_ASSIGN,
                                   .cancel_label = HINT_CANCEL,
                                   .rows = rows,
                                   .count = UI_CTRL_OUTPUT_COUNT,
                                   .focus = current_row >= 0 ? current_row : 0,
                               });
}

void ui_controller_mapping_close(void) {
  ui_list_popup_close(&s_popup);
}

bool ui_controller_mapping_is_open(void) {
  return ui_list_popup_is_open(&s_popup);
}

void ui_controller_mapping_open_shoulder(VitakiCtrlIn input, const char *subtitle) {
  s_target = (Target){.kind = TARGET_SHOULDER, .input = input};
  open_popup(TITLE_SHOULDER, subtitle, (int)ui_controller_model_output(input));
}

void ui_controller_mapping_open_zones(UiCtrlSide side, const int *zones, int count) {
  if (!zones || count <= 0 || count > UI_CTRL_ZONES) {
    LOGE("Controller mapping: cannot open the popup for %d zones", count);
    return;
  }
  s_target = (Target){.kind = TARGET_ZONES, .side = side, .zone_count = count};
  memcpy(s_target.zones, zones, (size_t)count * sizeof(zones[0]));

  const int current = ui_controller_model_zones_output(side, zones, count);
  char subtitle[SUBTITLE_MAX];
  if (count == 1) {
    snprintf(subtitle, sizeof(subtitle), SUBTITLE_ONE_FORMAT,
             side == UI_CTRL_SIDE_FRONT ? NAME_FRONT : NAME_REAR,
             ui_controller_zones_name(zones[0]));
  } else {
    char many[SUBTITLE_MAX];
    snprintf(many, sizeof(many), SUBTITLE_MANY_FORMAT, count);
    snprintf(subtitle, sizeof(subtitle), SUBTITLE_VALUE_FORMAT, many, value_name(current));
  }
  open_popup(side == UI_CTRL_SIDE_FRONT ? TITLE_FRONT : TITLE_REAR, subtitle, current);
}

void ui_controller_mapping_open_side(UiCtrlSide side) {
  s_target = (Target){.kind = TARGET_SIDE, .side = side};
  const int current = ui_controller_model_side_output(side);
  char subtitle[SUBTITLE_MAX];
  snprintf(subtitle, sizeof(subtitle), SUBTITLE_VALUE_FORMAT,
           side == UI_CTRL_SIDE_FRONT ? NAME_FULL_FRONT : NAME_FULL_REAR, value_name(current));
  open_popup(side == UI_CTRL_SIDE_FRONT ? TITLE_FRONT : TITLE_REAR, subtitle, current);
}

/** Assign @output to what the popup was opened for. */
static void assign_target(VitakiCtrlOut output) {
  switch (s_target.kind) {
    case TARGET_SHOULDER:
      ui_controller_model_assign(&s_target.input, 1, output);
      break;
    case TARGET_ZONES:
      ui_controller_model_assign_zones(s_target.side, s_target.zones, s_target.zone_count, output);
      break;
    case TARGET_SIDE:
      ui_controller_model_assign_side(s_target.side, output);
      break;
  }
}

bool ui_controller_mapping_update(const UiInput *in) {
  const UiEvent event = ui_list_popup_input(&s_popup, in);
  if (event == UI_EVENT_ACTIVATED) {
    assign_target(ui_controller_model_output_choice(s_popup.activated));
  } else if (event != UI_EVENT_CANCELLED) {
    return false;
  }
  ui_list_popup_close(&s_popup);
  return true;
}

void ui_controller_mapping_draw(void) {
  ui_list_popup_draw(&s_popup);
}

int ui_controller_mapping_hints(UiHintItem out[UI_HINT_MAX_ITEMS]) {
  return ui_list_popup_hints(&s_popup, out);
}
