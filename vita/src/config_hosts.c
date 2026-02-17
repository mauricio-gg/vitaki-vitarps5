#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <chiaki/base64.h>

#include "config_hosts.h"
#include "context.h"
#include "host.h"
#include "util.h"

static void zero_pad(char *buf, size_t size) {
  bool pad = false;
  for (size_t i = 0; i < size; i++) {
    if (buf[i] == '\0')
      pad = true;
    if (pad)
      buf[i] = '\0';
  }
  if (!pad && size > 0)
    buf[size - 1] = '\0';
}

static bool bytes_nonzero(const uint8_t *buf, size_t len) {
  if (!buf)
    return false;
  for (size_t i = 0; i < len; i++) {
    if (buf[i] != 0)
      return true;
  }
  return false;
}

static ChiakiTarget parse_target(const char *target_name) {
  if (strcmp("ps4_unknown", target_name) == 0) {
    return CHIAKI_TARGET_PS4_UNKNOWN;
  } else if (strcmp("ps4_8", target_name) == 0) {
    return CHIAKI_TARGET_PS4_8;
  } else if (strcmp("ps4_9", target_name) == 0) {
    return CHIAKI_TARGET_PS4_9;
  } else if (strcmp("ps4_10", target_name) == 0) {
    return CHIAKI_TARGET_PS4_10;
  } else if (strcmp("ps5_unknown", target_name) == 0) {
    return CHIAKI_TARGET_PS5_UNKNOWN;
  } else if (strcmp("ps5_1", target_name) == 0) {
    return CHIAKI_TARGET_PS5_1;
  }
  return CHIAKI_TARGET_PS4_UNKNOWN;
}

void config_parse_registered_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed) {
  toml_array_t *regist_hosts = toml_array_in(parsed, "registered_hosts");
  if (!regist_hosts || toml_array_kind(regist_hosts) != 't')
    return;

  int num_rhosts = toml_array_nelem(regist_hosts);
  for (int i = 0; i < MIN(MAX_REGISTERED_HOSTS, num_rhosts); i++) {
    if (cfg->num_registered_hosts >= MAX_REGISTERED_HOSTS) {
      CHIAKI_LOGW(&(context.log), "Registered host capacity reached; skipping entry %d", i);
      break;
    }
    VitaChiakiHost *host = malloc(sizeof(VitaChiakiHost));
    ChiakiRegisteredHost *rstate = malloc(sizeof(ChiakiRegisteredHost));
    if (!host || !rstate) {
      free(host);
      free(rstate);
      CHIAKI_LOGE(&(context.log), "Out of memory while parsing registered host entry %d", i);
      continue;
    }
    memset(host, 0, sizeof(VitaChiakiHost));
    memset(rstate, 0, sizeof(ChiakiRegisteredHost));
    host->registered_state = rstate;

    toml_table_t *host_cfg = toml_table_at(regist_hosts, i);
    toml_datum_t datum = toml_string_in(host_cfg, "server_mac");
    if (datum.ok) {
      parse_b64(datum.u.s, host->server_mac, 6);
      memcpy(&rstate->server_mac, &(host->server_mac), 6);
      free(datum.u.s);
    }

    datum = toml_string_in(host_cfg, "server_nickname");
    if (datum.ok) {
      strncpy(rstate->server_nickname, datum.u.s, sizeof(rstate->server_nickname));
      rstate->server_nickname[sizeof(rstate->server_nickname) - 1] = '\0';
      free(datum.u.s);
    }

    datum = toml_string_in(host_cfg, "target");
    if (datum.ok) {
      ChiakiTarget parsed_target = parse_target(datum.u.s);
      rstate->target = parsed_target;
      host->target = parsed_target;
      free(datum.u.s);
    }

    datum = toml_string_in(host_cfg, "rp_key");
    if (datum.ok) {
      parse_b64(datum.u.s, rstate->rp_key, 0x10);
      free(datum.u.s);
    }

    datum = toml_int_in(host_cfg, "rp_key_type");
    if (datum.ok)
      rstate->rp_key_type = datum.u.i;

    datum = toml_string_in(host_cfg, "rp_regist_key");
    if (datum.ok) {
      strncpy(rstate->rp_regist_key, datum.u.s, sizeof(rstate->rp_regist_key));
      zero_pad(rstate->rp_regist_key, sizeof(rstate->rp_regist_key));
      free(datum.u.s);
    }

    if (!bytes_nonzero(host->server_mac, sizeof(host->server_mac)) ||
        !bytes_nonzero(rstate->rp_key, sizeof(rstate->rp_key)) ||
        rstate->rp_regist_key[0] == '\0') {
      CHIAKI_LOGW(&(context.log), "Skipping invalid registered host entry %d (missing required fields)", i);
      host_free(host);
      continue;
    }

    cfg->registered_hosts[cfg->num_registered_hosts] = host;
    cfg->num_registered_hosts++;
  }
}

void config_parse_manual_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed) {
  toml_array_t *manual_hosts = toml_array_in(parsed, "manual_hosts");
  if (!manual_hosts || toml_array_kind(manual_hosts) != 't')
    return;

  int num_mhosts = toml_array_nelem(manual_hosts);
  LOGD("Found %d manual hosts", num_mhosts);
  for (int i = 0; i < MIN(MAX_MANUAL_HOSTS, num_mhosts); i++) {
    VitaChiakiHost *host = NULL;
    bool has_mac = false;
    bool has_hostname = false;

    toml_table_t *host_cfg = toml_table_at(manual_hosts, i);
    toml_datum_t datum = toml_string_in(host_cfg, "server_mac");
    MacAddr server_mac;
    if (datum.ok) {
      parse_b64(datum.u.s, server_mac, sizeof(server_mac));
      has_mac = true;
      free(datum.u.s);
      for (size_t hidx = 0; hidx < cfg->num_registered_hosts; hidx++) {
        if (mac_addrs_match(&server_mac, &(cfg->registered_hosts[hidx]->server_mac))) {
          host = malloc(sizeof(VitaChiakiHost));
          if (!host) {
            CHIAKI_LOGE(&(context.log), "Out of memory while cloning manual host entry %d", i);
            break;
          }
          copy_host(host, cfg->registered_hosts[hidx], false);
          host->type = REGISTERED;
          break;
        }
      }
    }

    if (!host) {
      CHIAKI_LOGW(&(context.log), "Manual host missing registered host.");
      continue;
    }

    host->type |= MANUALLY_ADDED;
    host->type &= ~DISCOVERED;

    datum = toml_string_in(host_cfg, "hostname");
    if (datum.ok) {
      // Takes ownership of toml-allocated string; host_free() releases it.
      host->hostname = datum.u.s;
      has_hostname = true;
    }

    if (has_hostname && has_mac) {
      size_t slot = cfg->num_manual_hosts;
      if (slot >= MAX_MANUAL_HOSTS) {
        CHIAKI_LOGW(&(context.log), "Manual host capacity reached, skipping entry %d", i);
        host_free(host);
        continue;
      }
      cfg->manual_hosts[slot] = host;
      cfg->num_manual_hosts++;
    } else {
      CHIAKI_LOGW(&(context.log), "Failed to parse manual host due to missing hostname or mac.");
      host_free(host);
    }
  }
}

static void serialize_b64(FILE *fp, const char *field_name, const uint8_t *val, size_t len) {
  bool all_zero = true;
  for (size_t i = 0; i < len; i++) {
    if (val[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero)
    return;

  fprintf(fp, "%s = \"", field_name);
  char b64[get_base64_size(len) + 1];
  memset(b64, 0, get_base64_size(len) + 1);
  chiaki_base64_encode(val, len, b64, get_base64_size(len));
  fprintf(fp, "%s\"\n", b64);
}

static void serialize_target(FILE *fp, const char *field_name, ChiakiTarget target) {
  fprintf(fp, "%s = \"", field_name);
  switch (target) {
    case CHIAKI_TARGET_PS4_UNKNOWN:
      fprintf(fp, "ps4_unknown");
      break;
    case CHIAKI_TARGET_PS4_8:
      fprintf(fp, "ps4_8");
      break;
    case CHIAKI_TARGET_PS4_9:
      fprintf(fp, "ps4_9");
      break;
    case CHIAKI_TARGET_PS4_10:
      fprintf(fp, "ps4_10");
      break;
    case CHIAKI_TARGET_PS5_UNKNOWN:
      fprintf(fp, "ps5_unknown");
      break;
    case CHIAKI_TARGET_PS5_1:
      fprintf(fp, "ps5_1");
      break;
  }
  fprintf(fp, "\"\n");
}

void config_serialize_manual_hosts(FILE *fp, const VitaChiakiConfig *cfg) {
  for (size_t i = 0; i < cfg->num_manual_hosts; i++) {
    VitaChiakiHost *host = cfg->manual_hosts[i];
    if (!host) {
      LOGD("config_serialize: manual host slot %zu is NULL, skipping", i);
      continue;
    }

    const uint8_t *mac = NULL;
    for (size_t m = 0; m < 6; m++) {
      if (host->server_mac[m] != 0) {
        mac = host->server_mac;
        break;
      }
    }
    if (!mac || !host->hostname) {
      LOGD("config_serialize: manual host slot %zu missing data, skipping", i);
      continue;
    }

    fprintf(fp, "\n\n[[manual_hosts]]\n");
    fprintf(fp, "hostname = \"%s\"\n", host->hostname);
    serialize_b64(fp, "server_mac", mac, 6);
  }
}

void config_serialize_registered_hosts(FILE *fp, const VitaChiakiConfig *cfg) {
  for (size_t i = 0; i < cfg->num_registered_hosts; i++) {
    VitaChiakiHost *host = cfg->registered_hosts[i];
    ChiakiRegisteredHost *rhost = host ? host->registered_state : NULL;
    if (!host || !rhost) {
      LOGD("config_serialize: registered host slot %zu missing data, skipping", i);
      continue;
    }

    fprintf(fp, "\n\n[[registered_hosts]]\n");
    serialize_b64(fp, "server_mac", rhost->server_mac, 6);
    fprintf(fp, "server_nickname = \"%s\"\n", rhost->server_nickname);
    serialize_target(fp, "target", rhost->target);
    serialize_b64(fp, "rp_key", rhost->rp_key, 0x10);
    fprintf(fp, "rp_key_type = %d\n", rhost->rp_key_type);
    fprintf(fp, "rp_regist_key = \"%s\"\n", rhost->rp_regist_key);
  }
}
