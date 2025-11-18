#include <sys/param.h>
#include <string.h>
#include <tomlc99/toml.h>
#include <chiaki/base64.h>

#include "config.h"
#include "context.h"
#include "host.h"
#include "util.h"

void zero_pad(char* buf, size_t size) {
  bool pad = false;
  for (int i=0; i < size; i++) {
    if (buf[i] == '\0') {
      pad = true;
    }
    if (pad) {
      buf[i] = '\0';
    }
  }
  if (!pad) {
    buf[size-1] = '\0';
  }
}

VitaChiakiDisconnectAction parse_disconnect_action(char* action) {
  if (strcmp(action, "ask") == 0) {
    return DISCONNECT_ACTION_ASK;
  } else if (strcmp(action, "rest") == 0) {
    return DISCONNECT_ACTION_REST;
  }
  return DISCONNECT_ACTION_NOTHING;
}

ChiakiVideoResolutionPreset parse_resolution_preset(char* preset) {
  if (strcmp(preset, "360p") == 0)
    return CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
  return CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
}

VitaChiakiLatencyMode parse_latency_mode(const char* mode) {
  if (!mode)
    return VITA_LATENCY_MODE_BALANCED;
  if (strcmp(mode, "ultra_low") == 0)
    return VITA_LATENCY_MODE_ULTRA_LOW;
  if (strcmp(mode, "low") == 0)
    return VITA_LATENCY_MODE_LOW;
  if (strcmp(mode, "high") == 0)
    return VITA_LATENCY_MODE_HIGH;
  if (strcmp(mode, "max") == 0)
    return VITA_LATENCY_MODE_MAX;
  return VITA_LATENCY_MODE_BALANCED;
}

const char* serialize_latency_mode(VitaChiakiLatencyMode mode) {
  switch (mode) {
    case VITA_LATENCY_MODE_ULTRA_LOW: return "ultra_low";
    case VITA_LATENCY_MODE_LOW: return "low";
    case VITA_LATENCY_MODE_HIGH: return "high";
    case VITA_LATENCY_MODE_MAX: return "max";
    case VITA_LATENCY_MODE_BALANCED:
    default:
      return "balanced";
  }
}

ChiakiTarget parse_target(char* target_name) {
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

bool get_circle_btn_confirm_default() {
  // Check system settings to see if circle should be select instead of cross
  // (should be true on Japanese vitas and false elsewhere).

	int button_assign = -1;
	int ret = 0;
	ret = sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "button_assign", &button_assign);
  if (ret < 0) {
    // Failed to determine. Just return false.
    return false;
  }

  // 0 => circle select; 1 => cross select; other values invalid (so default to false)
  return (button_assign == 0);
}

void config_parse(VitaChiakiConfig* cfg) {
  cfg->psn_account_id = NULL;
  cfg->auto_discovery = true;
  cfg->disconnect_action = DISCONNECT_ACTION_ASK;
  cfg->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
  cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30;
  cfg->controller_map_id = 0;
  cfg->show_latency = false;  // Default: latency display disabled
  cfg->latency_mode = VITA_LATENCY_MODE_BALANCED;
  cfg->stretch_video = false;
  cfg->force_30fps = false;

  bool circle_btn_confirm_default = get_circle_btn_confirm_default();
  cfg->circle_btn_confirm = circle_btn_confirm_default;

  if (access(CFG_FILENAME, F_OK) == 0) {
    FILE* fp = fopen(CFG_FILENAME, "r");
    char errbuf[200];
    toml_table_t* parsed = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!cfg) {
      CHIAKI_LOGE(&(context.log), "Failed to parse config due to illegal TOML: %s", errbuf);
      return NULL;
    }
    toml_table_t* general = toml_table_in(parsed, "general");
    if (!general) {
      CHIAKI_LOGE(&(context.log), "Failed to parse config due to missing [general] section");
      return NULL;
    }
    toml_datum_t datum;
    datum = toml_int_in(general, "version");
    if (!datum.ok || datum.u.i != CFG_VERSION) {
      CHIAKI_LOGE(&(context.log), "Failed to parse config due to bad general.version, expected %d.", CFG_VERSION);
      return NULL;
    }

    toml_table_t* settings = toml_table_in(parsed, "settings");
    if (settings) {
      datum = toml_bool_in(settings, "auto_discovery");
      cfg->auto_discovery = datum.ok ? datum.u.b : true;
      datum = toml_string_in(settings, "disconnect_action");
      if (datum.ok) {
        cfg->disconnect_action = parse_disconnect_action(datum.u.s);
        free(datum.u.s);
      } else {
        cfg->disconnect_action = DISCONNECT_ACTION_ASK;
      }
      datum = toml_string_in(settings, "resolution");
      if (datum.ok) {
        cfg->resolution = parse_resolution_preset(datum.u.s);
        free(datum.u.s);
      } else {
        cfg->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
      }

      datum = toml_int_in(settings, "fps");
      // set fps to 30 regardless of config file
      cfg->fps = CHIAKI_VIDEO_FPS_PRESET_30;
      if (datum.ok) {
        if (datum.u.i != 30) LOGD("Ignoring config file fps and setting to 30.");
      }

      datum = toml_string_in(settings, "psn_account_id");
      if (datum.ok) {
        cfg->psn_account_id = datum.u.s;
      }

      datum = toml_int_in(settings, "controller_map_id");
      if (datum.ok) {
        cfg->controller_map_id = datum.u.i;
      }

      datum = toml_bool_in(settings, "circle_btn_confirm");
      cfg->circle_btn_confirm = datum.ok ? datum.u.b : circle_btn_confirm_default;

      datum = toml_bool_in(settings, "show_latency");
      cfg->show_latency = datum.ok ? datum.u.b : false;  // Default: disabled

      datum = toml_bool_in(settings, "stretch_video");
      cfg->stretch_video = datum.ok ? datum.u.b : false;

      datum = toml_bool_in(settings, "force_30fps");
      cfg->force_30fps = datum.ok ? datum.u.b : false;

      datum = toml_string_in(settings, "latency_mode");
      if (datum.ok) {
        cfg->latency_mode = parse_latency_mode(datum.u.s);
        free(datum.u.s);
      } else {
        cfg->latency_mode = VITA_LATENCY_MODE_BALANCED;
      }
    }

    toml_array_t* regist_hosts = toml_array_in(parsed, "registered_hosts");
    if (regist_hosts && toml_array_kind(regist_hosts) == 't') {
      int num_rhosts = toml_array_nelem(regist_hosts);
      for (int i=0; i < MIN(MAX_NUM_HOSTS, num_rhosts); i++) {
        VitaChiakiHost* host = malloc(sizeof(VitaChiakiHost));
        ChiakiRegisteredHost* rstate = malloc(sizeof(ChiakiRegisteredHost));
        LOGD("Assigning registered state: 0x%x", rstate);
        host->registered_state = rstate;
        toml_table_t* host_cfg = toml_table_at(regist_hosts, i);
        datum = toml_string_in(host_cfg, "server_mac");
        if (datum.ok) {
          parse_b64(datum.u.s, host->server_mac, 6);
          printf("MAC %X%X%X%X%X%X\n", host->server_mac[0], host->server_mac[1], host->server_mac[2],
                          host->server_mac[3], host->server_mac[4], host->server_mac[5]);
          memcpy(&rstate->server_mac, &(host->server_mac), 6);
          free(datum.u.s);
        }
        datum = toml_string_in(host_cfg, "server_nickname");
        if (datum.ok) {
          strncpy(rstate->server_nickname, datum.u.s, sizeof(rstate->server_nickname));
          rstate->server_nickname[sizeof(rstate->server_nickname)-1] = '\0';
          free(datum.u.s);
        }
        datum = toml_string_in(host_cfg, "target");
        if (datum.ok) {
          rstate->target = parse_target(datum.u.s);
          host->target = parse_target(datum.u.s);
          free(datum.u.s);
        }
        datum = toml_string_in(host_cfg, "rp_key");
        if (datum.ok) {
          printf("after rp %s\n", datum.u.s);
          parse_b64(datum.u.s, rstate->rp_key, 0x10);
          hexdump(rstate->rp_key, (size_t)0x10);
          free(datum.u.s);
        }
        datum = toml_int_in(host_cfg, "rp_key_type");
        if (datum.ok) {
          rstate->rp_key_type = datum.u.i;
        }
        datum = toml_string_in(host_cfg, "rp_regist_key");
        if (datum.ok) {
          strncpy(rstate->rp_regist_key, datum.u.s, sizeof(rstate->rp_regist_key));
          zero_pad(rstate->rp_regist_key, sizeof(rstate->rp_regist_key));
          free(datum.u.s);
        }
        // datum = toml_string_in(host_cfg, "ap_bssid");
        // if (datum.ok) {
        //   strncpy(rstate->ap_bssid, datum.u.s, sizeof(rstate->ap_bssid));
        //   zero_pad(rstate->ap_bssid, sizeof(rstate->ap_bssid));
        //   free(datum.u.s);
        // }
        // datum = toml_string_in(host_cfg, "ap_key");
        // if (datum.ok) {
        //   strncpy(rstate->ap_key, datum.u.s, sizeof(rstate->ap_key));
        //   zero_pad(rstate->ap_key, sizeof(rstate->ap_key));
        //   free(datum.u.s);
        // }
        // datum = toml_string_in(host_cfg, "ap_ssid");
        // if (datum.ok) {
        //   strncpy(rstate->ap_ssid, datum.u.s, sizeof(rstate->ap_ssid));
        //   zero_pad(rstate->ap_ssid, sizeof(rstate->ap_ssid));
        //   free(datum.u.s);
        // }
        // datum = toml_string_in(host_cfg, "ap_name");
        // if (datum.ok) {
        //   strncpy(rstate->ap_name, datum.u.s, sizeof(rstate->ap_name));
        //   zero_pad(rstate->ap_name, sizeof(rstate->ap_name));
        //   free(datum.u.s);
        // }
        cfg->registered_hosts[i] = host;
        cfg->num_registered_hosts++;
      }
    }

    toml_array_t* manual_hosts = toml_array_in(parsed, "manual_hosts");
    if (manual_hosts && toml_array_kind(manual_hosts) == 't') {
      int num_mhosts = toml_array_nelem(manual_hosts);
      LOGD("Found %d manual hosts", num_mhosts);
      for (int i=0; i < MIN(MAX_NUM_HOSTS, num_mhosts) ; i++) {
        VitaChiakiHost* host = NULL;

        bool has_mac = false;
        bool has_hostname = false;
        bool has_registration = false;

        toml_table_t* host_cfg = toml_table_at(manual_hosts, i);
        datum = toml_string_in(host_cfg, "server_mac");
        uint8_t server_mac[6];
        if (datum.ok) {
          // We have a MAC for the manual host, try to find corresponding
          // registered host
          parse_b64(datum.u.s, server_mac, sizeof(server_mac));
          has_mac = true;
          free(datum.u.s);
          for (int hidx=0; hidx < cfg->num_registered_hosts; hidx++) {
            uint8_t* candidate_mac = cfg->registered_hosts[hidx]->server_mac;
            if (candidate_mac) {
              if (mac_addrs_match(&server_mac, candidate_mac)) {
                // copy registered host (TODO for the registered_state, should we use a pointer instead?)
                host = malloc(sizeof(VitaChiakiHost));
                copy_host(host, cfg->registered_hosts[hidx], false);
                host->type = REGISTERED;
                has_registration = true;
                break;
              }
            }
          }
        }
        if (!host) {
          // No corresponding registered host found. Don't save.
          CHIAKI_LOGW(&(context.log), "Manual host missing registered host.");
          continue;
        }

        host->type |= MANUALLY_ADDED;
        host->type &= ~DISCOVERED; // ensure discovered is off

        datum = toml_string_in(host_cfg, "hostname");
        if (datum.ok) {
          host->hostname = datum.u.s;
          has_hostname = true;
        }

        if (has_hostname && has_mac) {
          cfg->manual_hosts[i] = host;
          cfg->num_manual_hosts++;
        } else {
          CHIAKI_LOGW(&(context.log), "Failed to parse manual host due to missing hostname or mac.");
          free(host);
        }
      }
    }
  }
}

void config_free(VitaChiakiConfig* cfg) {
  if (cfg == NULL) {
    return;
  }
  free(cfg->psn_account_id);
  for (int i = 0; i < MAX_NUM_HOSTS; i++) {
    if (cfg->manual_hosts[i] != NULL) {
      host_free(cfg->manual_hosts[i]);
    }
    if (cfg->registered_hosts[i] != NULL) {
      host_free(cfg->registered_hosts[i]);
    }
  }
  free(cfg);
}

char* serialize_disconnect_action(VitaChiakiDisconnectAction action) {
  switch (action) {
    case DISCONNECT_ACTION_ASK:
      return "ask";
    case DISCONNECT_ACTION_REST:
      return "rest";
    case DISCONNECT_ACTION_NOTHING:
      return "nothing";
  }
}

char* serialize_resolution_preset(ChiakiVideoResolutionPreset preset) {
  switch (preset) {
    case CHIAKI_VIDEO_RESOLUTION_PRESET_360p:
      return "360p";
    default:
      // We don't support solutions higher than 540p on the Vita
      // TODO: Should we? For PSTV?
      return "540p";
  }
}

void serialize_b64(FILE* fp, char* field_name, uint8_t* val, size_t len) {
  bool all_zero = true;
  for (size_t i=0; i < len; i++) {
    if (val[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    return;
  }
  fprintf(fp, "%s = \"", field_name);
  // for (size_t i=0; i < len; i++) {
  //   fprintf(fp, "%02X", val[i]);
  // }
  char b64[get_base64_size(len) + 1];
  memset(b64, 0, get_base64_size(len) + 1);
  chiaki_base64_encode(val, len, b64, get_base64_size(len));
  fprintf(fp, "%s\"\n", b64);
}

void serialize_target(FILE* fp, char* field_name, ChiakiTarget* target) {
  fprintf(fp, "%s = \"", field_name);
  switch (*target) {
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

void config_serialize(VitaChiakiConfig* cfg) {
  FILE *fp = fopen(CFG_FILENAME, "w");
  fprintf(fp, "[general]\nversion = 1\n");

  // Settings
  fprintf(fp, "[settings]\n");
  fprintf(fp, "auto_discovery = %s\n",
          cfg->auto_discovery ? "true" : "false");
  fprintf(fp, "disconnect_action = \"%s\"\n",
          serialize_disconnect_action(cfg->disconnect_action));
  fprintf(fp, "resolution = \"%s\"\n",
          serialize_resolution_preset(cfg->resolution));
  fprintf(fp, "fps = %d\n", cfg->fps);
  if (cfg->psn_account_id) {
    fprintf(fp, "psn_account_id = \"%s\"\n", cfg->psn_account_id);
  }
  fprintf(fp, "controller_map_id = %d\n", cfg->controller_map_id);
  fprintf(fp, "circle_btn_confirm = %s\n",
          cfg->circle_btn_confirm ? "true" : "false");
  fprintf(fp, "show_latency = %s\n",
          cfg->show_latency ? "true" : "false");
  fprintf(fp, "stretch_video = %s\n",
          cfg->stretch_video ? "true" : "false");
  fprintf(fp, "force_30fps = %s\n",
          cfg->force_30fps ? "true" : "false");
  fprintf(fp, "latency_mode = \"%s\"\n", serialize_latency_mode(cfg->latency_mode));

  for (int i = 0; i < cfg->num_manual_hosts; i++) {
    VitaChiakiHost* host = cfg->manual_hosts[i];
    uint8_t* mac = NULL;
    for (int m = 0; m < 6; m++) {
      if (host->server_mac[m] != 0) {
        mac = host->server_mac;
        break;
      }
    }
    if (mac) {
      // only save if mac is valid
      fprintf(fp, "\n\n[[manual_hosts]]\n");
      fprintf(fp, "hostname = \"%s\"\n", host->hostname);
      serialize_b64(fp, "server_mac", mac, 6);
    }
  }

  for (int i = 0; i < cfg->num_registered_hosts; i++) {
    ChiakiRegisteredHost* rhost = cfg->registered_hosts[i]->registered_state;
    fprintf(fp, "\n\n[[registered_hosts]]\n");
    serialize_b64(fp, "server_mac", rhost->server_mac, 6);
    fprintf(fp, "server_nickname = \"%s\"\n", rhost->server_nickname);
    serialize_target(fp, "target", &rhost->target);
    serialize_b64(fp, "rp_key", rhost->rp_key, 0x10);
    fprintf(fp, "rp_key_type = %d\n", rhost->rp_key_type);
    fprintf(fp, "rp_regist_key = \"%s\"\n", rhost->rp_regist_key);
    // fprintf(fp, "ap_bssid = \"%s\"\n", rhost->ap_bssid);
    // fprintf(fp, "ap_key = \"%s\"\n", rhost->ap_key);
    // fprintf(fp, "ap_ssid = \"%s\"\n", rhost->ap_ssid);
    // fprintf(fp, "ap_name = \"%s\"\n", rhost->ap_name);
  }
  fclose(fp);
}
