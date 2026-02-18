#include <stdlib.h>

#include "context.h"
#include "config.h"
#include "psn_auth.h"

bool psn_auth_enabled(void) {
  return context.config.psn_remoteplay_enabled;
}

bool psn_auth_has_tokens(void) {
  return context.config.psn_oauth_access_token &&
         context.config.psn_oauth_access_token[0] &&
         context.config.psn_oauth_refresh_token &&
         context.config.psn_oauth_refresh_token[0];
}

bool psn_auth_token_is_valid(uint64_t now_unix) {
  if (!psn_auth_enabled() || !context.config.psn_oauth_access_token ||
      !context.config.psn_oauth_access_token[0]) {
    return false;
  }
  if (context.config.psn_oauth_expires_at_unix == 0)
    return false;
  return now_unix < context.config.psn_oauth_expires_at_unix;
}

const char *psn_auth_access_token(void) {
  return context.config.psn_oauth_access_token;
}

void psn_auth_clear_tokens(void) {
  free(context.config.psn_oauth_access_token);
  context.config.psn_oauth_access_token = NULL;
  free(context.config.psn_oauth_refresh_token);
  context.config.psn_oauth_refresh_token = NULL;
  context.config.psn_oauth_expires_at_unix = 0;
}
