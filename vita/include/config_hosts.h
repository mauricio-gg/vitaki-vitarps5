#pragma once

#include <stdio.h>
#include <tomlc99/toml.h>

#include "config.h"

void config_parse_registered_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed);
void config_parse_manual_hosts(VitaChiakiConfig *cfg, toml_table_t *parsed);
void config_serialize_manual_hosts(FILE *fp, const VitaChiakiConfig *cfg);
void config_serialize_registered_hosts(FILE *fp, const VitaChiakiConfig *cfg);
