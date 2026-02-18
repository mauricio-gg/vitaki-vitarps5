#pragma once

#include <stdbool.h>
#include <stdint.h>

bool psn_auth_enabled(void);
bool psn_auth_has_tokens(void);
bool psn_auth_token_is_valid(uint64_t now_unix);
const char *psn_auth_access_token(void);
void psn_auth_clear_tokens(void);
