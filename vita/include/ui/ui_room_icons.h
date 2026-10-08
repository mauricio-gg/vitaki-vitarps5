#pragma once

/*
 * Room icon textures (SPEC C02, C03, C12 grid variant). Every size of every icon is loaded
 * once at init and kept for the life of the app; drawing code only looks one up.
 */

#include <stdbool.h>
#include <vita2d.h>

#include "config.h"
#include "room_icons.h"

/** Pixel sizes the room icon art is drawn at; each has its own PNG so nothing is rescaled. */
typedef enum room_icon_size_t {
  ROOM_ICON_SIZE_ROW = 0,  // Home list rows (UI_ITEM_ICON)
  ROOM_ICON_SIZE_GRID,     // Change-icon picker grid (UI_ROOM_ICON_GRID)
  ROOM_ICON_SIZE_RING,     // Connecting ring centre (UI_ROOM_ICON_RING)
  ROOM_ICON_SIZE_COUNT
} RoomIconSize;

/** Load every room icon texture. Safe to call again; it loads only what is missing. */
void ui_room_icons_init(void);

/**
 * ui_room_icons_get() - Texture for a room icon at a size.
 * @icon: A RoomIcon value; out of range is shown as the TV.
 * @size: A RoomIconSize value.
 * Return: The texture, or NULL when it failed to load or @size is invalid.
 */
vita2d_texture *ui_room_icons_get(int icon, RoomIconSize size);

/**
 * ui_room_icon_for_host() - Icon of a console shown in the UI.
 * @host: May be NULL. A console without a MAC (no identity to store a choice under) gets
 *        ROOM_ICON_TV.
 */
int ui_room_icon_for_host(const VitaChiakiHost *host);

/**
 * ui_room_icon_set_for_host() - Choose an icon for a console and save the config.
 * @host: The console. One without a MAC cannot be changed.
 * @icon: A RoomIcon value.
 * Return: true when the choice was stored and written to the config file.
 */
bool ui_room_icon_set_for_host(const VitaChiakiHost *host, int icon);
