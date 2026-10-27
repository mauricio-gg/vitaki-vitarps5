#pragma once

#include <stdio.h>
#include <tomlc99/toml.h>

#include "config.h"

/*
 * config_parse_registered_hosts — Load registered consoles from the parsed config.
 *
 * Pairing keys come from the encrypted rp_key_enc / rp_regist_key_enc fields. If an _enc field is
 * absent the legacy plain-text key is used instead and *migrated_plaintext_keys is set so the
 * caller rewrites the file encrypted. A host whose _enc field is present but will not decrypt
 * (e.g. the memory card moved to another Vita) is dropped, never read from plain text.
 */
void config_parse_registered_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed,
                                   bool *migrated_plaintext_keys);
void config_parse_manual_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed);
void config_serialize_manual_hosts(FILE *fp, const VitaChiakiConfig *cfg);

/*
 * config_serialize_registered_hosts — Write registered consoles with encrypted pairing keys.
 *
 * Returns false if any host's keys could not be encrypted (logged with the host nickname). The
 * caller must then discard the output: plain-text keys are never written as a fallback.
 */
bool config_serialize_registered_hosts(FILE *fp, const VitaChiakiConfig *cfg);
