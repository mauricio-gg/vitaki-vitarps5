#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <chiaki/base64.h>

#include "config_hosts.h"
#include "context.h"
#include "host.h"
#include "token_crypto.h"
#include "util.h"

/* Both pairing keys are exactly CHIAKI_SESSION_AUTH_SIZE / rp_key bytes (16) of raw data. */
#define PAIRING_KEY_LEN 0x10
#define PAIRING_KEY_B64_BUF_SIZE 32

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

/*
 * load_encrypted_pairing_key — Decrypt one pairing key stored as base64 text inside an _enc blob.
 *
 * @param host_cfg  TOML table of the registered host.
 * @param toml_key  Key of the encrypted blob ("rp_key_enc" / "rp_regist_key_enc").
 * @param kind      TOKEN_CRYPTO_KIND_* the blob was encrypted with.
 * @param out       Receives exactly PAIRING_KEY_LEN raw bytes; zeroed on failure.
 *
 * Returns TOKEN_LOAD_ABSENT if the key is not in the table, TOKEN_LOAD_OK on success and
 * TOKEN_LOAD_FAILED if it is present but will not decrypt or does not decode to exactly
 * PAIRING_KEY_LEN bytes. Key material is never logged.
 */
static TokenLoadResult load_encrypted_pairing_key(toml_table_t *host_cfg, const char *toml_key,
                                                  const char *kind, uint8_t out[PAIRING_KEY_LEN]) {
  toml_datum_t datum = toml_string_in(host_cfg, toml_key);
  if (!datum.ok)
    return TOKEN_LOAD_ABSENT;

  char *plain = token_crypto_decrypt(datum.u.s, kind);
  free(datum.u.s);
  if (!plain)
    return TOKEN_LOAD_FAILED;

  size_t decoded_len = PAIRING_KEY_LEN;
  ChiakiErrorCode err = chiaki_base64_decode(plain, strlen(plain), out, &decoded_len);
  free(plain);
  if (err != CHIAKI_ERR_SUCCESS || decoded_len != PAIRING_KEY_LEN) {
    memset(out, 0, PAIRING_KEY_LEN);
    return TOKEN_LOAD_FAILED;
  }
  return TOKEN_LOAD_OK;
}

void config_parse_registered_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed,
                                   bool *migrated_plaintext_keys) {
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
    host->type = REGISTERED;
    host->source = VITA_HOST_SOURCE_LOCAL_DISCOVERY;
    host->remoteplay_enabled = false;
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
    // Nickname is the only name available for a registered host loaded straight from config
    // (no discovery/PSN refresh has run yet); honors the precedence documented in host.h:48-51.
    snprintf(host->display_name, sizeof(host->display_name), "%s", rstate->server_nickname);

    datum = toml_string_in(host_cfg, "target");
    if (datum.ok) {
      ChiakiTarget parsed_target = parse_target(datum.u.s);
      rstate->target = parsed_target;
      host->target = parsed_target;
      free(datum.u.s);
    }

    /* Encrypted keys win. A plain-text key is read only when its _enc key is absent (legacy
     * file, migrated on the next save); a present-but-undecryptable _enc key drops the host. */
    bool host_used_plaintext = false;
    TokenLoadResult rp_key_result = load_encrypted_pairing_key(
        host_cfg, "rp_key_enc", TOKEN_CRYPTO_KIND_RP_KEY, rstate->rp_key);
    if (rp_key_result == TOKEN_LOAD_ABSENT) {
      datum = toml_string_in(host_cfg, "rp_key");
      if (datum.ok) {
        parse_b64(datum.u.s, rstate->rp_key, PAIRING_KEY_LEN);
        free(datum.u.s);
        host_used_plaintext = true;
      }
    }

    datum = toml_int_in(host_cfg, "rp_key_type");
    if (datum.ok)
      rstate->rp_key_type = datum.u.i;

    TokenLoadResult regist_key_result =
        load_encrypted_pairing_key(host_cfg, "rp_regist_key_enc", TOKEN_CRYPTO_KIND_RP_REGIST_KEY,
                                   (uint8_t *)rstate->rp_regist_key);
    if (regist_key_result == TOKEN_LOAD_ABSENT) {
      datum = toml_string_in(host_cfg, "rp_regist_key");
      if (datum.ok) {
        strncpy(rstate->rp_regist_key, datum.u.s, sizeof(rstate->rp_regist_key));
        zero_pad(rstate->rp_regist_key, sizeof(rstate->rp_regist_key));
        free(datum.u.s);
        host_used_plaintext = true;
      }
    }

    if (rp_key_result == TOKEN_LOAD_FAILED || regist_key_result == TOKEN_LOAD_FAILED) {
      const uint8_t *m = host->server_mac;
      CHIAKI_LOGE(&(context.log),
                  "Pairing keys of registered host entry %d (nickname=%s mac=%02x:%02x:%02x:%02x:"
                  "%02x:%02x) will not decrypt (config from another Vita?); dropping host, pair "
                  "again",
                  i, rstate->server_nickname[0] ? rstate->server_nickname : "<unnamed>", m[0], m[1],
                  m[2], m[3], m[4], m[5]);
      host_free(host);
      continue;
    }

    if (!bytes_nonzero(host->server_mac, sizeof(host->server_mac)) ||
        !bytes_nonzero(rstate->rp_key, sizeof(rstate->rp_key)) ||
        rstate->rp_regist_key[0] == '\0') {
      CHIAKI_LOGW(&(context.log),
                  "Skipping invalid registered host entry %d (missing required fields)", i);
      host_free(host);
      continue;
    }

    if (host_used_plaintext)
      *migrated_plaintext_keys = true;
    cfg->registered_hosts[cfg->num_registered_hosts] = host;
    LOGD("Loaded registered host[%zu]: nickname=%s target=%d type=0x%x", cfg->num_registered_hosts,
         rstate->server_nickname[0] ? rstate->server_nickname : "<unnamed>", host->target,
         host->type);
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
          host = calloc(1, sizeof(VitaChiakiHost));
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
    host->source = VITA_HOST_SOURCE_MANUAL_REMOTE;

    datum = toml_string_in(host_cfg, "hostname");
    if (datum.ok) {
      // hostname is an inline array now, not a heap pointer -- copy in and release the
      // toml-allocated string ourselves instead of transferring ownership to the host.
      snprintf(host->hostname, sizeof(host->hostname), "%s", datum.u.s);
      if (!host->display_name[0])
        snprintf(host->display_name, sizeof(host->display_name), "%s", datum.u.s);
      free(datum.u.s);
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
    if (!mac || !host->hostname[0]) {
      LOGE("config_serialize: manual host slot %zu NOT saved: %s%s%s", i, mac ? "" : "MAC is zero",
           (!mac && !host->hostname[0]) ? " and " : "",
           host->hostname[0] ? "" : "hostname is empty");
      continue;
    }

    fprintf(fp, "\n\n[[manual_hosts]]\n");
    fprintf(fp, "hostname = \"%s\"\n", host->hostname);
    serialize_b64(fp, "server_mac", mac, 6);
  }
}

/*
 * serialize_encrypted_pairing_key — Write one pairing key as an encrypted base64 blob.
 *
 * The 16 raw bytes are base64-encoded (so a full buffer with no NUL terminator is safe) and the
 * text is encrypted with @p kind. An all-zero key is not written, matching serialize_b64.
 * Returns false only if encryption failed; nothing is written then. Key material is never logged.
 */
static bool serialize_encrypted_pairing_key(FILE *fp, const char *field_name,
                                            const uint8_t key[PAIRING_KEY_LEN], const char *kind) {
  if (!bytes_nonzero(key, PAIRING_KEY_LEN))
    return true;

  char b64[PAIRING_KEY_B64_BUF_SIZE];
  memset(b64, 0, sizeof(b64));
  if (chiaki_base64_encode(key, PAIRING_KEY_LEN, b64, sizeof(b64)) != CHIAKI_ERR_SUCCESS)
    return false;

  char *blob = token_crypto_encrypt(b64, kind);
  if (!blob)
    return false;

  fprintf(fp, "%s = \"%s\"\n", field_name, blob);
  free(blob);
  return true;
}

bool config_serialize_registered_hosts(FILE *fp, const VitaChiakiConfig *cfg) {
  bool ok = true;
  for (size_t i = 0; i < cfg->num_registered_hosts; i++) {
    VitaChiakiHost *host = cfg->registered_hosts[i];
    ChiakiRegisteredHost *rhost = host ? host->registered_state : NULL;
    if (!host || !rhost) {
      LOGD("config_serialize: registered host slot %zu missing data, skipping", i);
      continue;
    }

    fprintf(fp, "\n\n[[registered_hosts]]\n");
    serialize_b64(fp, "server_mac", host->server_mac, 6);
    fprintf(fp, "server_nickname = \"%s\"\n", rhost->server_nickname);
    serialize_target(fp, "target", rhost->target);
    fprintf(fp, "rp_key_type = %u\n", rhost->rp_key_type);
    if (!serialize_encrypted_pairing_key(fp, "rp_key_enc", rhost->rp_key,
                                         TOKEN_CRYPTO_KIND_RP_KEY) ||
        !serialize_encrypted_pairing_key(fp, "rp_regist_key_enc",
                                         (const uint8_t *)rhost->rp_regist_key,
                                         TOKEN_CRYPTO_KIND_RP_REGIST_KEY)) {
      CHIAKI_LOGE(&(context.log),
                  "Could not encrypt pairing keys of registered host slot %zu (nickname=%s); "
                  "refusing to save",
                  i, rhost->server_nickname[0] ? rhost->server_nickname : "<unnamed>");
      ok = false;
    }
  }
  return ok;
}
