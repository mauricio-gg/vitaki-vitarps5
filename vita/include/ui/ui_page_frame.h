/**
 * @file ui_page_frame.h
 * @brief C07 PageShell frame: wash, title icon, title and rule (SPEC.md C07)
 *
 * Display-only draw helper for the pages that are not Home (Connecting, Reconnecting, Settings,
 * PIN, Profile and Controller). Draw it after the wave and before the top bar
 * and the page body. Paper cost: 4 draws (wash, icon, title, rule).
 */

#pragma once

#include <stdbool.h>

#include <vita2d.h>

#include "ui/ui_component.h"

/** The 32 px icon in the title row. */
typedef enum ui_page_icon_t {
  UI_PAGE_ICON_LAN = 0,
  UI_PAGE_ICON_GLOBE,
  UI_PAGE_ICON_MOON,
  UI_PAGE_ICON_WIFI,
  UI_PAGE_ICON_GEAR,
  UI_PAGE_ICON_LOCK,
  UI_PAGE_ICON_PROFILE,
  UI_PAGE_ICON_CONTROLLER,
  UI_PAGE_ICON_COUNT
} UiPageIcon;

/** ui_page_frame_init() - Load the title icons. Call once at start-up. */
void ui_page_frame_init(void);

/**
 * ui_page_frame_icon() - The loaded 32 px texture of @icon, for other components that show the
 * same art (the Re-pair popup shows the lock). NULL for an unknown icon or one that failed to load.
 */
vita2d_texture *ui_page_frame_icon(UiPageIcon icon);

/**
 * ui_page_frame_draw() - Draw the page wash, then the title row and its rule.
 * @icon:  Which 32 px icon (a missing texture draws no icon).
 * @title: Page title, T28 at x UI_PAGE_TITLE_X.
 */
void ui_page_frame_draw(UiPageIcon icon, const char *title);

/**
 * ui_page_frame_back_draw() - Draw the back chevron at the left of the title row, in TEXT_2. Call
 * it after ui_page_frame_draw(); a missing texture draws nothing. Paper cost: 1 draw.
 */
void ui_page_frame_back_draw(void);

/**
 * ui_page_frame_back_tapped() - True when this frame's input is a tap that lifted inside the back
 * chevron's 48 x 48 hit box (x 0, UI_TITLE_Y). The page decides what going back means.
 */
bool ui_page_frame_back_tapped(const UiInput *in);
