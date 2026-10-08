#include "ui/ui_room_icons.h"

#include <stdio.h>

#include "context.h"
#include "ui/ui_internal.h"
#include "ui/ui_theme.h"

#define ROOM_ICON_DIR "app0:/assets/icons/"
#define ROOM_ICON_PATH_MAX 96

/* File stems in RoomIcon order, and the pixel size each RoomIconSize file is cut at. The size
 * is part of the file name, room_<stem>_<px>.png. */
static const char *const ROOM_ICON_STEMS[ROOM_ICON_COUNT] = {
    "tv", "sofa", "bed", "bunk", "desk", "house",
};
static const int ROOM_ICON_PIXELS[ROOM_ICON_SIZE_COUNT] = {
    UI_ITEM_ICON,
    UI_ROOM_ICON_GRID,
    UI_ROOM_ICON_RING,
};

static vita2d_texture *s_textures[ROOM_ICON_COUNT][ROOM_ICON_SIZE_COUNT];

void ui_room_icons_init(void) {
  char path[ROOM_ICON_PATH_MAX];
  for (int icon = 0; icon < ROOM_ICON_COUNT; icon++) {
    for (int size = 0; size < ROOM_ICON_SIZE_COUNT; size++) {
      if (s_textures[icon][size])
        continue;
      snprintf(path, sizeof(path), ROOM_ICON_DIR "room_%s_%d.png", ROOM_ICON_STEMS[icon],
               ROOM_ICON_PIXELS[size]);
      s_textures[icon][size] = ui_load_png_linear(path);
    }
  }
}

vita2d_texture *ui_room_icons_get(int icon, RoomIconSize size) {
  if (size < 0 || size >= ROOM_ICON_SIZE_COUNT)
    return NULL;
  if (icon < 0 || icon >= ROOM_ICON_COUNT)
    icon = ROOM_ICON_TV;
  return s_textures[icon][size];
}

int ui_room_icon_for_host(const VitaChiakiHost *host) {
  if (!host)
    return ROOM_ICON_TV;
  return room_icons_get(&context.config.room_icons, host->server_mac);
}

bool ui_room_icon_can_change(const VitaChiakiHost *host) {
  return host && room_icons_can_store(host->server_mac);
}

bool ui_room_icon_set_for_host(const VitaChiakiHost *host, int icon) {
  if (!host)
    return false;
  RoomIconTable *table = &context.config.room_icons;
  const int previous = room_icons_get(table, host->server_mac);
  if (!room_icons_set(table, host->server_mac, icon))
    return false;
  if (!config_serialize(&context.config)) {
    LOGE("Failed to save room icon choice for console; keeping icon %d", previous);
    room_icons_set(table, host->server_mac, previous);
    return false;
  }
  return true;
}
