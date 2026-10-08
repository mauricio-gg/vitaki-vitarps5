#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "room_icons.h"

#define MAC_BYTES 6
#define MAC_HEX_LEN (MAC_BYTES * 2)

/* Labels in RoomIcon order (copy deck, SPEC section 5 "Change icon"). */
static const char *const ROOM_ICON_LABELS[ROOM_ICON_COUNT] = {
    "TV", "Living room", "Bedroom", "Dorm", "Office", "Another place",
};

static bool mac_is_zero(const uint8_t mac[MAC_BYTES]) {
  for (size_t i = 0; i < MAC_BYTES; i++) {
    if (mac[i] != 0)
      return false;
  }
  return true;
}

/* Index of the entry for this MAC, or -1. */
static int find_entry(const RoomIconTable *table, const uint8_t mac[MAC_BYTES]) {
  for (size_t i = 0; i < table->count; i++) {
    if (memcmp(table->entries[i].mac, mac, MAC_BYTES) == 0)
      return (int)i;
  }
  return -1;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* Parse exactly 12 hex digits into a MAC. Anything else is refused. */
static bool parse_mac_hex(const char *text, uint8_t mac[MAC_BYTES]) {
  if (!text || strlen(text) != MAC_HEX_LEN)
    return false;
  for (size_t i = 0; i < MAC_BYTES; i++) {
    int hi = hex_digit(text[i * 2]);
    int lo = hex_digit(text[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return false;
    mac[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

const char *room_icon_label(int icon) {
  if (icon < 0 || icon >= ROOM_ICON_COUNT)
    icon = ROOM_ICON_TV;
  return ROOM_ICON_LABELS[icon];
}

int room_icons_get(const RoomIconTable *table, const uint8_t mac[MAC_BYTES]) {
  if (!table || !mac || mac_is_zero(mac))
    return ROOM_ICON_TV;
  int idx = find_entry(table, mac);
  return idx < 0 ? ROOM_ICON_TV : table->entries[idx].icon;
}

bool room_icons_set(RoomIconTable *table, const uint8_t mac[MAC_BYTES], int icon) {
  if (!table || !mac || mac_is_zero(mac) || icon < 0 || icon >= ROOM_ICON_COUNT)
    return false;

  int idx = find_entry(table, mac);
  if (icon == ROOM_ICON_TV) {
    if (idx >= 0) {
      table->entries[idx] = table->entries[table->count - 1];
      table->count--;
    }
    return true;
  }
  if (idx < 0) {
    if (table->count >= MAX_ROOM_ICON_ENTRIES) {
      LOGE("Room icon table full (%d); cannot store another console", MAX_ROOM_ICON_ENTRIES);
      return false;
    }
    idx = (int)table->count++;
    memcpy(table->entries[idx].mac, mac, MAC_BYTES);
  }
  table->entries[idx].icon = (uint8_t)icon;
  return true;
}

void room_icons_parse(RoomIconTable *table, toml_table_t *parsed) {
  table->count = 0;
  toml_array_t *array = toml_array_in(parsed, "room_icons");
  if (!array || toml_array_kind(array) != 't')
    return;

  int num = toml_array_nelem(array);
  for (int i = 0; i < num; i++) {
    toml_table_t *entry = toml_table_at(array, i);
    toml_datum_t mac_datum = toml_string_in(entry, "mac");
    toml_datum_t icon_datum = toml_int_in(entry, "icon");
    uint8_t mac[MAC_BYTES];
    bool mac_ok = mac_datum.ok && parse_mac_hex(mac_datum.u.s, mac) && !mac_is_zero(mac);
    if (mac_datum.ok)
      free(mac_datum.u.s);

    if (!mac_ok || !icon_datum.ok || icon_datum.u.i <= ROOM_ICON_TV ||
        icon_datum.u.i >= ROOM_ICON_COUNT) {
      CHIAKI_LOGW(&(context.log), "Ignoring invalid room_icons entry %d", i);
      continue;
    }
    if (find_entry(table, mac) >= 0) {
      CHIAKI_LOGW(&(context.log), "Ignoring repeated room_icons entry %d", i);
      continue;
    }
    if (!room_icons_set(table, mac, (int)icon_datum.u.i))
      break;
  }
}

void room_icons_serialize(FILE *fp, const RoomIconTable *table) {
  for (size_t i = 0; i < table->count; i++) {
    const RoomIconEntry *e = &table->entries[i];
    fprintf(fp, "\n\n[[room_icons]]\nmac = \"%02x%02x%02x%02x%02x%02x\"\nicon = %d\n", e->mac[0],
            e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5], (int)e->icon);
  }
}
