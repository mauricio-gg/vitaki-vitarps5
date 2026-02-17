#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "host.h"

void host_set_hint(VitaChiakiHost *host, const char *msg, bool is_error, uint64_t duration_us);
void host_request_decoder_resync(const char *reason);
void host_handle_unrecovered_frame_loss(int32_t frames_lost, bool frame_recovered);
void host_handle_takion_overflow(void);
void host_handle_loss_event(int32_t frames_lost, bool frame_recovered);
