#pragma once

#include <inttypes.h>
#include <sys/types.h>

void parse_b64(const char *val, uint8_t *dest, size_t len);

void parse_mac(const char *mac_str, uint8_t *mac_dest);

void utf16_to_utf8(const uint16_t *src, uint8_t *dst);

void utf8_to_utf16(const uint8_t *src, uint16_t *dst);

size_t get_base64_size(size_t in);

int init_msg_dialog(const char *msg);

int get_msg_dialog_result(void);
