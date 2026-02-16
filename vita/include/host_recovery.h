#pragma once

#include <stdbool.h>
#include <stdint.h>

void host_recovery_handle_post_reconnect_degraded_mode(bool av_diag_progressed,
                                                       uint32_t incoming_fps,
                                                       uint32_t target_fps,
                                                       bool low_fps_window,
                                                       uint64_t now_us);
