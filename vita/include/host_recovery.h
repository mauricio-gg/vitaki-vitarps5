#pragma once

#include <stdbool.h>
#include <stdint.h>

void host_recovery_handle_post_reconnect_degraded_mode(bool av_diag_progressed,
                                                       uint32_t incoming_fps, uint32_t target_fps,
                                                       bool low_fps_window, uint64_t now_us);

// Requests a soft stream restart on behalf of the packet-loss recovery gate's tier 3
// (vita/src/host_feedback.c, host_handle_loss_event()) -- reached when repeated loss bursts
// within the gate's window persist after tiers 1-2 (IDR-only, then a resync follow-up)
// already failed to clear them. Thin, source-labeled wrapper around
// request_stream_restart_coordinated() so the loss gate gets the same guards (stop/fast-
// restart-active checks, cooloff, max-reconnect cap, bitrate clamp, reconnect overlay) as
// the post-reconnect recovery ladder's stage2/stage3 callers, without duplicating them.
// Returns false if the restart was refused or failed -- the caller must fall back to a
// resync rather than leaving the user with nothing.
bool host_recovery_request_loss_gate_restart(uint32_t bitrate_kbps, uint64_t now_us);
