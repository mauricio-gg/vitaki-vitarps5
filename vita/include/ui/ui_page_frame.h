/**
 * @file ui_page_frame.h
 * @brief C07 PageShell frame: wash, title icon, title and rule (SPEC.md C07)
 *
 * Display-only draw helper for the pages that are not Home (Connecting, Reconnecting, Settings,
 * PIN, Profile, and the Controller page later). Draw it after the wave and before the top bar
 * and the page body. Paper cost: 4 draws (wash, icon, title, rule).
 */

#pragma once

#include <vita2d.h>

/** The 32 px icon in the title row. */
typedef enum ui_page_icon_t {
  UI_PAGE_ICON_LAN = 0,
  UI_PAGE_ICON_GLOBE,
  UI_PAGE_ICON_MOON,
  UI_PAGE_ICON_WIFI,
  UI_PAGE_ICON_GEAR,
  UI_PAGE_ICON_LOCK,
  UI_PAGE_ICON_PROFILE,
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
