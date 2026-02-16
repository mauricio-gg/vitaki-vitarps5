#pragma once

#include <stdbool.h>

void host_metrics_reset_stream(bool preserve_recovery_state);
void host_metrics_update_latency(void);
