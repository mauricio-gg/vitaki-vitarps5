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

void save_manual_host(VitaChiakiHost *rhost, char *new_hostname) {
  (void)rhost;
  (void)new_hostname;
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

void copy_host_registered_state(ChiakiRegisteredHost *rstate_dest, ChiakiRegisteredHost *rstate_src) {
  if (rstate_dest && rstate_src)
    memcpy(rstate_dest, rstate_src, sizeof(*rstate_dest));
}

void parse_b64(const char *val, uint8_t *dest, size_t len) {
  (void)val;
  memset(dest, 0, len);
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

ChiakiErrorCode chiaki_base64_encode(const uint8_t *in, size_t in_size, char *out, size_t out_size) {
  (void)in;
  (void)in_size;
  if (!out || out_size == 0)
    return CHIAKI_ERR_BUF_TOO_SMALL;
  out[0] = '\0';
  return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode chiaki_base64_decode(const char *in, size_t in_size, uint8_t *out, size_t *out_size) {
  (void)in;
  (void)in_size;
  if (out && out_size)
    memset(out, 0, *out_size);
  return CHIAKI_ERR_SUCCESS;
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
  assert(cfg.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);
  assert(cfg.show_latency == true);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "resolution = \"720p\"") != NULL);
  assert(strstr(rewritten, "fps = 60") != NULL);
  assert(strstr(rewritten, "show_latency = true") != NULL);
  assert(count_occurrences(rewritten, "resolution = \"720p\"") == 1);
  free(rewritten);
}

static void test_root_level_fallback_migration(void) {
  reset_config_file();
  write_config_text(
      "resolution = \"1080p\"\n"
      "fps = 60\n"
      "keep_nav_pinned = true\n"
      "\n"
      "[general]\n"
      "version = 1\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);
  assert(cfg.resolution == CHIAKI_VIDEO_RESOLUTION_PRESET_720p);
  assert(cfg.fps == CHIAKI_VIDEO_FPS_PRESET_60);
  assert(cfg.keep_nav_pinned == true);

  char *rewritten = read_config_text();
  assert(strstr(rewritten, "[settings]") != NULL);
  assert(strstr(rewritten, "resolution = \"720p\"") != NULL);
  assert(strstr(rewritten, "resolution = \"1080p\"") == NULL);
  assert(strstr(rewritten, "fps = 60") != NULL);
  assert(strstr(rewritten, "keep_nav_pinned = true") != NULL);
  assert(count_occurrences(rewritten, "resolution = \"720p\"") == 1);
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
      {CHIAKI_VIDEO_RESOLUTION_PRESET_720p, CHIAKI_VIDEO_RESOLUTION_PRESET_720p, "720p"},
      {CHIAKI_VIDEO_RESOLUTION_PRESET_1080p, CHIAKI_VIDEO_RESOLUTION_PRESET_720p, "720p"},
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
    if (cases[i].input_preset == CHIAKI_VIDEO_RESOLUTION_PRESET_1080p) {
      assert(strstr(saved, "1080p") == NULL);
    }
    free(saved);
  }
}

int main(void) {
  test_legacy_section_migration();
  test_root_level_fallback_migration();
  test_invalid_fps_falls_back_to_30();
  test_resolution_roundtrip();
  reset_config_file();
  puts("vitarps5 config tests passed");
  return 0;
}
