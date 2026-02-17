#pragma once

#include <stdbool.h>

#include <chiaki/session.h>

const char *host_quit_reason_label(ChiakiQuitReason reason);
bool host_quit_reason_requires_retry(ChiakiQuitReason reason);
void host_update_disconnect_banner(const char *reason);
