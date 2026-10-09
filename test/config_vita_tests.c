#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "chiaki/base64.h"
#include "config.h"
#include "context.h"

VitaChiakiContext context = {0};

void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...) {
  (void)log;
  (void)level;
  (void)fmt;
}

void host_free(VitaChiakiHost *host) {
  if (!host)
    return;
  free(host->registered_state);
  free(host->hostname);
  free(host);
}

int host_register(VitaChiakiHost *host, int pin) {
  (void)host;
  (void)pin;
  return 0;
}

int host_wakeup(VitaChiakiHost *host) {
  (void)host;
  return 0;
}

int host_stream(VitaChiakiHost *host) {
  (void)host;
  return 0;
}

void host_cancel_stream_request(void) {}

bool mac_addrs_match(MacAddr *a, MacAddr *b) {
  return memcmp(a, b, sizeof(MacAddr)) == 0;
}

bool save_manual_host(VitaChiakiHost *rhost, char *new_hostname) {
  (void)rhost;
  (void)new_hostname;
  return true;
}

void delete_manual_host(VitaChiakiHost *mhost) {
  (void)mhost;
}

void update_context_hosts(void) {}

int count_manual_hosts_of_console(VitaChiakiHost *host) {
  (void)host;
  return 0;
}

void copy_host(VitaChiakiHost *h_dest, VitaChiakiHost *h_src, bool copy_hostname) {
  (void)copy_hostname;
  if (h_dest && h_src)
    memcpy(h_dest, h_src, sizeof(*h_dest));
}

void copy_host_registered_state(ChiakiRegisteredHost *rstate_dest, const ChiakiRegisteredHost *rstate_src) {
  if (rstate_dest && rstate_src)
    memcpy(rstate_dest, rstate_src, sizeof(*rstate_dest));
}

void parse_b64(const char *val, uint8_t *dest, size_t len) {
  memset(dest, 0, len);
  if (!val || !val[0] || !dest || len == 0)
    return;
  for (size_t i = 0; i < len; i++) {
    dest[i] = (uint8_t)((i + 1u) & 0xFFu);
  }
}

void parse_mac(const char *mac_str, uint8_t *mac_dest) {
  (void)mac_str;
  memset(mac_dest, 0, 6);
}

void utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
  (void)src;
  if (dst)
    *dst = '\0';
}

void utf8_to_utf16(const uint8_t *src, uint16_t *dst) {
  (void)src;
  if (dst)
    *dst = 0;
}

size_t get_base64_size(size_t in) {
  return ((4 * in / 3) + 3) & ~3U;
}

int init_msg_dialog(const char *msg) {
  (void)msg;
  return 0;
}

int get_msg_dialog_result(void) {
  return 1;
}

void vita_logging_config_set_defaults(VitaLoggingConfig *cfg) {
  if (!cfg)
    return;
  memset(cfg, 0, sizeof(*cfg));
  cfg->enabled = false;
  cfg->force_error_logging = true;
  cfg->profile = VITA_LOG_PROFILE_ERRORS;
  cfg->queue_depth = VITA_LOG_DEFAULT_QUEUE_DEPTH;
  strncpy(cfg->path, VITA_LOG_DEFAULT_PATH, sizeof(cfg->path) - 1);
}

VitaLogProfile vita_logging_profile_from_string(const char *value) {
  if (!value)
    return VITA_LOG_PROFILE_STANDARD;
  if (strcmp(value, "off") == 0)
    return VITA_LOG_PROFILE_OFF;
  if (strcmp(value, "errors") == 0)
    return VITA_LOG_PROFILE_ERRORS;
  if (strcmp(value, "verbose") == 0)
    return VITA_LOG_PROFILE_VERBOSE;
  return VITA_LOG_PROFILE_STANDARD;
}

const char *vita_logging_profile_to_string(VitaLogProfile profile) {
  switch (profile) {
    case VITA_LOG_PROFILE_OFF:
      return "off";
    case VITA_LOG_PROFILE_ERRORS:
      return "errors";
    case VITA_LOG_PROFILE_VERBOSE:
      return "verbose";
    case VITA_LOG_PROFILE_STANDARD:
    default:
      return "standard";
  }
}

uint32_t vita_logging_profile_mask(VitaLogProfile profile) {
  (void)profile;
  return 0;
}

void vita_log_module_init(const VitaLoggingConfig *cfg) {
  (void)cfg;
}

void vita_log_module_shutdown(void) {}

void vita_log_submit_line(ChiakiLogLevel level, const char *line) {
  (void)level;
  (void)line;
}

bool vita_log_should_write_level(ChiakiLogLevel level) {
  (void)level;
  return false;
}

const VitaLoggingConfig *vita_log_get_active_config(void) {
  return NULL;
}

void controller_map_storage_set_defaults(ControllerMapStorage *storage) {
  if (!storage)
    return;
  memset(storage, 0, sizeof(*storage));
}

int sceRegMgrGetKeyInt(const char *category, const char *name, int *out_value) {
  (void)category;
  (void)name;
  if (out_value)
    *out_value = 1;
  return 0;
}

void hexdump(const uint8_t *data, size_t len) {
  (void)data;
  (void)len;
}

static void reset_config_file(void) {
  remove(CFG_FILENAME);
}

static void write_config_text(const char *text) {
  FILE *fp = fopen(CFG_FILENAME, "w");
  assert(fp != NULL);
  size_t len = strlen(text);
  assert(fwrite(text, 1, len, fp) == len);
  assert(fclose(fp) == 0);
}

static char *read_config_text(void) {
  FILE *fp = fopen(CFG_FILENAME, "r");
  assert(fp != NULL);
  assert(fseek(fp, 0, SEEK_END) == 0);
  long size = ftell(fp);
  assert(size >= 0);
  assert(fseek(fp, 0, SEEK_SET) == 0);
  char *buf = malloc((size_t)size + 1);
  assert(buf != NULL);
  size_t read = fread(buf, 1, (size_t)size, fp);
  assert(read == (size_t)size);
  buf[size] = '\0';
  assert(fclose(fp) == 0);
  return buf;
}

static int count_occurrences(const char *haystack, const char *needle) {
  int count = 0;
  const char *cursor = haystack;
  size_t needle_len = strlen(needle);
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count++;
    cursor += needle_len;
  }
  return count;
}

static void init_cfg(VitaChiakiConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  config_parse(cfg);
}

static void test_legacy_section_migration(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "auto_discovery = true\n"
      "controller_map_id = 201\n"
      "\n"
      "[controller_custom_map_1]\n"
      "valid = false\n"
      "in_l2 = 0\n"
      "in_r2 = 0\n"
      "resolution = \"720p\"\n"
      "fps = 60\n"
      "show_latency = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_540p);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);
  assert(cfg.show_latency == true);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "resolution = \"540p\"") != NULL);
  assert(strstr(rewritten, "fps = 60") != NULL);
  assert(strstr(rewritten, "show_latency = true") != NULL);
  assert(count_occurrences(rewritten, "resolution = \"540p\"") == 1);
  free(rewritten);
}

static void test_root_level_fallback_migration(void) {
  reset_config_file();
  write_config_text(
      "resolution = \"1080p\"\n"
      "fps = 60\n"
      "\n"
      "[general]\n"
      "version = 1\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_540p);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "resolution = \"540p\"") != NULL);
  assert(strstr(rewritten, "resolution = \"1080p\"") == NULL);
  assert(strstr(rewritten, "fps = 60") != NULL);
  assert(count_occurrences(rewritten, "resolution = \"540p\"") == 1);
  free(rewritten);
}

static void test_legacy_bool_and_latency_migration(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "\n"
      "[controller_custom_map_2]\n"
      "valid = false\n"
      "in_l2 = 0\n"
      "in_r2 = 0\n"
      "show_network_indicator = false\n"
      "show_stream_exit_hint = false\n"
      "send_actual_start_bitrate = false\n"
      "latency_mode = \"max\"\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_network_indicator == false);
  assert(cfg.show_stream_exit_hint == false);
  assert(cfg.send_actual_start_bitrate == false);
  assert(cfg.latency_mode == VITA_LATENCY_MODE_MAX);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "show_network_indicator = false") != NULL);
  assert(strstr(rewritten, "show_stream_exit_hint = false") != NULL);
  assert(strstr(rewritten, "send_actual_start_bitrate = false") != NULL);
  assert(strstr(rewritten, "latency_mode = \"max\"") != NULL);
  free(rewritten);
}

static void test_root_level_bool_migration(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "stretch_video = true\n"
      "clamp_soft_restart_bitrate = false\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.stretch_video == true);
  assert(cfg.clamp_soft_restart_bitrate == false);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "stretch_video = true") != NULL);
  assert(strstr(rewritten, "clamp_soft_restart_bitrate = false") != NULL);
  free(rewritten);
}

static void test_invalid_fps_falls_back_to_30(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "fps = 42\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_30);
  assert(config_serialize(&cfg));

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "fps = 30") != NULL);
  free(rewritten);
}

static void test_resolution_roundtrip(void) {
  const struct {
    ChiakiVideoResolutionPreset input_preset;
    ChiakiVideoResolutionPreset expected_preset;
    const char *expected_label;
  } cases[] = {
      {CHIAKI_VIDEO_RESOLUTION_PRESET_360p, CHIAKI_VIDEO_RESOLUTION_PRESET_360p, "360p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_540p, CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_720p, CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, CHIAKI_VIDEO_RESOLUTION_PRESET_540p, "540p"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    reset_config_file();

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    cfg.resolution = cases[i].input_preset;
    assert(config_serialize(&cfg));

    VitaChiakiConfig loaded;
    init_cfg(&loaded);
    assert(loaded.resolution == cases[i].expected_preset);

    char *saved = read_config_text();
    assert(strstr(saved, cases[i].expected_label) != NULL);
    if (cases[i].input_preset == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p ||
        cases[i].input_preset == CHIAKI_VIDEO_RESOLUTION_PRESET_720p) {
      assert(strstr(saved, "1080p") == NULL);
      assert(strstr(saved, "720p") == NULL);
    }
    free(saved);
  }
}

static void test_registered_hosts_require_required_fields(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[[registered_hosts]]\n"
      "target = \"ps5_1\"\n"
      "rp_key_type = 2\n"
      "server_nickname = \"invalid\"\n"
      "\n"
      "[[registered_hosts]]\n"
      "server_mac = \"AQIDBAUG\"\n"
      "server_nickname = \"valid\"\n"
      "target = \"ps5_1\"\n"
      "rp_key = \"AQIDBAUGBwgJCgsMDQ4PEA==\"\n"
      "rp_key_type = 2\n"
      "rp_regist_key = \"0011223344556677\"\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.num_registered_hosts == 1);
  assert(cfg.registered_hosts[0] != NULL);
  assert(cfg.registered_hosts[0]->registered_state != NULL);
  assert(cfg.registered_hosts[0]->target == CHIAKI_TARGET_PS5_1);
  assert(cfg.registered_hosts[0]->registered_state->rp_regist_key[0] != '\0');
}

/* catches: a config saved before these keys existed loading with blur or hints wrong, or the
 * new keys disturbing values that were already there. */
static void test_old_config_gets_new_field_defaults(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "fps = 60\n"
      "show_latency = true\n"
      "stretch_video = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.background_blur == VITA_BACKGROUND_BLUR_NONE);
  assert(cfg.show_button_hints == true);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);
  assert(cfg.show_latency == true);
  assert(cfg.stretch_video == true);
}

/* catches: the removed Show Only Paired key lingering in the config file forever (written back
 * on every save), or an old file that still carries it failing to load its other settings. */
static void test_removed_show_only_paired_key_is_dropped(void) {
  reset_config_file();
  write_config_text(
      "[general]\n"
      "version = 1\n"
      "\n"
      "[settings]\n"
      "controller_map_id = 201\n"
      "show_only_paired = true\n"
      "show_latency = true\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.show_latency == true);
  assert(config_serialize(&cfg));

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "show_only_paired") == NULL);
  assert(strstr(rewritten, "show_latency = true") != NULL);
  free(rewritten);
}

/* catches: a non-default blur level or hints-off choice being lost on save or load, so the
 * setting reverts after a restart. */
static void test_new_fields_survive_save_and_load(void) {
  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  cfg.background_blur = VITA_BACKGROUND_BLUR_STRONG;
  cfg.show_button_hints = false;
  assert(config_serialize(&cfg));

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(loaded.background_blur == VITA_BACKGROUND_BLUR_STRONG);
  assert(loaded.show_button_hints == false);
}

/* catches: a hand-edited or corrupt blur value outside 0..3 being trusted, which would index
 * past the background levels. */
static void test_out_of_range_blur_is_rejected(void) {
  const int bad_values[] = {-1, 4, 99};
  for (size_t i = 0; i < sizeof(bad_values) / sizeof(bad_values[0]); i++) {
    char text[160];
    snprintf(text, sizeof(text),
             "[general]\nversion = 1\n\n[settings]\ncontroller_map_id = 201\nbackground_blur = %d\n",
             bad_values[i]);
    reset_config_file();
    write_config_text(text);

    VitaChiakiConfig cfg;
    init_cfg(&cfg);
    assert(cfg.background_blur == VITA_BACKGROUND_BLUR_NONE);
  }
}

/* catches: a console's chosen room icon being lost on save or load (so every console reverts to
 * the TV after a restart), or one console's choice landing on another console. */
static void test_room_icons_survive_save_and_load(void) {
  const uint8_t mac_a[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e};
  const uint8_t mac_b[6] = {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa};
  const uint8_t mac_other[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5f};

  reset_config_file();
  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(room_icons_set(&cfg.room_icons, mac_a, ROOM_ICON_BEDROOM));
  assert(room_icons_set(&cfg.room_icons, mac_b, ROOM_ICON_ANOTHER_PLACE));
  assert(config_serialize(&cfg));

  VitaChiakiConfig loaded;
  init_cfg(&loaded);
  assert(room_icons_get(&loaded.room_icons, mac_a) == ROOM_ICON_BEDROOM);
  assert(room_icons_get(&loaded.room_icons, mac_b) == ROOM_ICON_ANOTHER_PLACE);
  assert(room_icons_get(&loaded.room_icons, mac_other) == ROOM_ICON_TV);
}

/* catches: a hand-edited or corrupt room_icons entry being trusted: an icon outside the art
 * (which would index past the textures), a malformed or all-zero MAC, or a repeated MAC
 * overriding the first. The one good entry must still load. */
static void test_bad_room_icon_entries_are_ignored(void) {
  const uint8_t good_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  reset_config_file();
  write_config_text(
      "[general]\nversion = 1\n\n[settings]\ncontroller_map_id = 201\n"
      "\n[[room_icons]]\nmac = \"010203040506\"\nicon = 4\n"
      "\n[[room_icons]]\nmac = \"0a0b0c0d0e0f\"\nicon = 99\n"
      "\n[[room_icons]]\nmac = \"0a0b0c0d0e0f\"\nicon = -1\n"
      "\n[[room_icons]]\nmac = \"0a0b0c0d0e0f\"\nicon = 0\n"
      "\n[[room_icons]]\nmac = \"0102\"\nicon = 2\n"
      "\n[[room_icons]]\nmac = \"zz0203040506\"\nicon = 2\n"
      "\n[[room_icons]]\nmac = \"000000000000\"\nicon = 2\n"
      "\n[[room_icons]]\nmac = \"010203040506\"\nicon = 3\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.room_icons.count == 1);
  assert(room_icons_get(&cfg.room_icons, good_mac) == ROOM_ICON_OFFICE);
}

/* catches: a console with no MAC being given a stored icon (it has no identity, so the choice
 * would leak onto every other MAC-less console); an out-of-range choice being stored; choosing
 * the TV leaving a stale entry that comes back after a restart; and a full table overwriting or
 * corrupting existing choices. */
static void test_room_icon_set_rules(void) {
  const uint8_t no_mac[6] = {0};
  const uint8_t mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
  RoomIconTable table = {0};

  assert(!room_icons_set(&table, no_mac, ROOM_ICON_DORM));
  assert(room_icons_get(&table, no_mac) == ROOM_ICON_TV);
  assert(!room_icons_set(&table, mac, ROOM_ICON_COUNT));
  assert(!room_icons_set(&table, mac, -1));
  assert(table.count == 0);

  assert(room_icons_set(&table, mac, ROOM_ICON_DORM));
  assert(room_icons_set(&table, mac, ROOM_ICON_TV));
  assert(table.count == 0);
  assert(room_icons_get(&table, mac) == ROOM_ICON_TV);

  for (int i = 0; i < MAX_ROOM_ICON_ENTRIES; i++) {
    const uint8_t fill[6] = {0xaa, 0, 0, 0, 0, (uint8_t)(i + 1)};
    assert(room_icons_set(&table, fill, ROOM_ICON_OFFICE));
  }
  assert(!room_icons_set(&table, mac, ROOM_ICON_DORM));
  assert(room_icons_get(&table, mac) == ROOM_ICON_TV);
  const uint8_t first[6] = {0xaa, 0, 0, 0, 0, 1};
  assert(room_icons_set(&table, first, ROOM_ICON_BEDROOM));
  assert(room_icons_get(&table, first) == ROOM_ICON_BEDROOM);
  assert(table.count == MAX_ROOM_ICON_ENTRIES);
}

void run_packet_path_tests(void);
void run_json_escape_tests(void);
void run_token_crypto_tests(void);

int main(void) {
  test_legacy_section_migration();
  test_root_level_fallback_migration();
  test_legacy_bool_and_latency_migration();
  test_root_level_bool_migration();
  test_invalid_fps_falls_back_to_30();
  test_resolution_roundtrip();
  test_registered_hosts_require_required_fields();
  test_old_config_gets_new_field_defaults();
  test_removed_show_only_paired_key_is_dropped();
  test_new_fields_survive_save_and_load();
  test_out_of_range_blur_is_rejected();
  test_room_icons_survive_save_and_load();
  test_bad_room_icon_entries_are_ignored();
  test_room_icon_set_rules();
  run_packet_path_tests();
  run_json_escape_tests();
  run_token_crypto_tests();
  reset_config_file();
  puts("vitarps5 config tests passed");
  return 0;
}
