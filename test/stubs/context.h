#pragma once

#include <chiaki/log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "controller.h"

typedef struct vita_chiaki_context_t {
  ChiakiLog log;
  struct {
    bool is_streaming;
  } stream;
  void *mlog;
  /* The controller maps controller.c reads (its custom preset slots). */
  struct {
    ControllerMapStorage custom_maps[3];
    bool custom_maps_valid[3];
  } config;
} VitaChiakiContext;

extern VitaChiakiContext context;
int sceRegMgrGetKeyInt(const char *category, const char *name, int *out_value);
void hexdump(const uint8_t *data, size_t len);

#define LOGD(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
