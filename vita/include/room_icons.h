#pragma once

/*
 * Room icons: the small picture a console wears on the Home list and the Connecting ring
 * (SPEC C02, C12 grid variant). Each console remembers one icon across restarts. The choice
 * is stored per console MAC in the config file, so it survives re-registration and also works
 * for consoles that are not paired.
 *
 * The table and its lookups are pure data (no vita2d, no file access) so the native test
 * suite can run them. Textures live in ui/ui_room_icons.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <tomlc99/toml.h>
/** Room icon indices, in picker order. ROOM_ICON_TV is the default. */
typedef enum room_icon_t {
  ROOM_ICON_TV = 0,
  ROOM_ICON_LIVING_ROOM,
  ROOM_ICON_BEDROOM,
  ROOM_ICON_DORM,
  ROOM_ICON_OFFICE,
  ROOM_ICON_ANOTHER_PLACE,
  ROOM_ICON_COUNT
} RoomIcon;

/** Most consoles that can carry a non-default icon at once (the host list holds up to 64). */
#define MAX_ROOM_ICON_ENTRIES 32

/** One console's chosen icon. Only non-default choices are stored. */
typedef struct room_icon_entry_t {
  uint8_t mac[6];
  uint8_t icon;  // RoomIcon, 1 .. ROOM_ICON_COUNT - 1
} RoomIconEntry;

typedef struct room_icon_table_t {
  size_t count;
  RoomIconEntry entries[MAX_ROOM_ICON_ENTRIES];
} RoomIconTable;

/**
 * room_icon_label() - Display name of a room icon ("TV", "Living room", ...).
 * @icon: A RoomIcon value; anything out of range is treated as ROOM_ICON_TV.
 * Return: A static string.
 */
const char *room_icon_label(int icon);

/**
 * room_icons_get() - Icon chosen for the console with this MAC.
 * @table: The table to search.
 * @mac:   Console MAC; a NULL or all-zero MAC has no identity and gets ROOM_ICON_TV.
 * Return: A RoomIcon in range, ROOM_ICON_TV when nothing was chosen.
 */
int room_icons_get(const RoomIconTable *table, const uint8_t mac[6]);

/**
 * room_icons_can_store() - Whether a console with this MAC has an identity to store a choice under.
 * @mac: Console MAC; a NULL or all-zero MAC has none.
 */
bool room_icons_can_store(const uint8_t mac[6]);

/**
 * room_icons_set() - Remember an icon for the console with this MAC (in memory only).
 * Choosing ROOM_ICON_TV frees the console's entry.
 * @table: The table to change.
 * @mac:   Console MAC; a NULL or all-zero MAC cannot be stored.
 * @icon:  A RoomIcon value; out of range is refused.
 * Return: true when the table now holds the choice; false when refused or the table is full.
 */
bool room_icons_set(RoomIconTable *table, const uint8_t mac[6], int icon);

/**
 * room_icons_parse() - Read the [[room_icons]] array of a parsed config.
 * Entries with a bad MAC, an out-of-range or default icon, or a repeated MAC are skipped
 * and logged. A missing array leaves the table empty.
 */
void room_icons_parse(RoomIconTable *table, toml_table_t *parsed);

/** room_icons_serialize() - Write one [[room_icons]] block per stored entry. */
void room_icons_serialize(FILE *fp, const RoomIconTable *table);
