#pragma once

#include <chiaki/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct vita_chiaki_context_t {
  ChiakiLog log;
  struct {
    bool is_streaming;
  } stream;
  void *mlog;
} VitaChiakiContext;

extern VitaChiakiContext context;
int sceRegMgrGetKeyInt(const char *category, const char *name, int *out_value);
void hexdump(const uint8_t *data, size_t len);

#define LOGD(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
