/**
 * @file ui_detail_panel.h
 * @brief C04 DetailPanel: facts about the focused item, no card (SPEC.md C04)
 *
 * A display-only draw helper. The screen describes what to show in a UiDetailContent and calls
 * ui_detail_panel_draw() once per frame. The panel keeps three small pieces of file-static
 * state and nothing else: the rise timer, the wrapped status message and the fitted text
 * widths. The wrap and the widths are recomputed only when the text they were made for changes,
 * never per frame, and everything lives in fixed buffers.
 *
 * Draw cost (logical draws, a text run counts as one): a console is logo 1 + name 1 + status
 * dot and text 2 + 3 kv rows x 3 = 13, plus one per message line. A list-kind panel is title 1
 * + description 0 or 1 + 3 per row with a value (2 without one).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <vita2d.h>

#include "ui/ui_type_logo.h"

/** Most kv rows a panel shows (a Settings group has five today). */
#define UI_DETAIL_MAX_ROWS 6

typedef enum ui_detail_kind_t {
  UI_DETAIL_NONE = 0,  ///< nothing is focused; the panel draws nothing
  UI_DETAIL_CONSOLE,   ///< type logo, name, status line, optional message, kv rows
  UI_DETAIL_LIST,      ///< title, optional description line, kv rows
} UiDetailKind;

/** One label and value row. A NULL or empty value draws the label only (an action row). */
typedef struct ui_detail_row_t {
  const char *label;
  const char *value;
  uint32_t value_color;  ///< 0 draws the value in UI_TEXT
} UiDetailRow;

/** What to show. Strings are borrowed for the call; the panel copies what it caches. */
typedef struct ui_detail_content_t {
  UiDetailKind kind;
  uint32_t key;  ///< identity of the focused item; the panel rises again when it changes

  const char *title;  ///< console name or list title

  /* UI_DETAIL_CONSOLE */
  vita2d_texture *logo;  ///< PS5_logo.png or ps4.png; NULL draws no logo
  UiTypeLogo logo_kind;
  const char *status;  ///< status label drawn after a dot, in status_color
  uint32_t status_color;
  const char *message;  ///< status message under the status line, or NULL
  uint32_t message_color;

  /* UI_DETAIL_LIST */
  const char *description;  ///< line under the title, or NULL

  UiDetailRow rows[UI_DETAIL_MAX_ROWS];
  int row_count;
} UiDetailContent;

/**
 * ui_detail_panel_draw() - Draw the panel at UI_DETAIL_X, UI_DETAIL_Y.
 * @content: What to show. The panel rises (UI_RISE_PX, fade) over UI_D2_MS whenever
 *           @content->key differs from the previous call.
 */
void ui_detail_panel_draw(const UiDetailContent *content);

/** ui_detail_panel_restart_rise() - Make the next draw rise again (Home was just entered). */
void ui_detail_panel_restart_rise(void);
