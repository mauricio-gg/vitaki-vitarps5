/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015, 2016 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */
#include <ctype.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/apputil.h>
#include <psp2/message_dialog.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/libssl.h>
#include <psp2/rtc.h>
#include <psp2/sysmodule.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
//#include <debugnet.h>

#include "context.h"
#include "discovery.h"
#include "ui.h"
#include "ui/ui_controller_diagram.h"

static int vita_init() {
  // Overclock various aspects of the vita
  scePowerSetArmClockFrequency(444);
  scePowerSetGpuClockFrequency(222);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);
  // Seed OpenSSL
  char random_seed[0x40] = {0};
  sceKernelGetRandomNumber(random_seed, sizeof(random_seed));
  RAND_seed(random_seed, sizeof(random_seed));
  OpenSSL_add_all_algorithms();

  LOGI("Vita Chiaki %s", CHIAKI_VERSION);

  sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
  sceCommonDialogSetConfigParam(&(SceCommonDialogConfigParam){});

  // int ret = 0;

  int ret;

  if ((ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET)) < 0) {
    LOGE("SCE_SYSMODULE_NET loading failed: %x\n", ret);
    return 1;
  }

  SceNetInitParam param;
  // TODO: this is probably way too large
  static char memory[4 * 1024 * 1024];
  param.memory = memory;
  param.size = sizeof(memory);
  param.flags = 0;

  if ((ret = sceNetInit(&param)) < 0) {
    LOGE("sceNetInit failed: %x\n", ret);
    return 1;
  }

  if ((ret = sceNetCtlInit()) < 0) {
    LOGE("sceNetCtlInit failed %x\n", ret);
    return 1;
  }

  // if ((ret = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP)) < 0) {
  //   LOGE("SCE_SYSMODULE_HTTP loading failed: %x\n", ret);
  //   return 1;
  // }

  // if ((ret = sceHttpInit(1024 * 1024)) < 0) {
  //   LOGE("sceHttpInit failed: %x\n", ret);
  //   return 1;
  // }

  // if ((ret = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS)) < 0) {
  //   LOGE("SCE_SYSMODULE_HTTPS loading failed: %x\n", ret);
  //   return 1;
  // }

  if ((ret = sceSysmoduleLoadModule(SCE_SYSMODULE_SSL)) < 0) {
    LOGE("SCE_SYSMODULE_SSL loading failed: %x\n", ret);
    return 1;
  }

  if ((ret = sceSslInit(1024 * 1024)) < 0) {
    LOGE("SslInit failed: %x\n", ret);
    return 1;
  }

  LOGD("Network initialized.\n");

  if ((ret = sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC)) < 0) {
    LOGE("SCE_SYSMODULE_AVCDEC loading failed: %x\n", ret);
    return 1;
  }

  // Initialize touch sampling
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
}

// set newlib heap size to 64mb, default is 32mb
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;

int main(int argc, char* argv[]) {
  vita_init();

  // Note: Power management is configured in vita_init() (scePowerSet* calls)
  // Note: Input thread is created per-stream in host.c when streaming starts

  sceIoMkdir("ux0:/data/vita-chiaki", 0777);

  vita_chiaki_init_context();
  if (context.config.auto_discovery) {
    LOGD("Starting discovery");
    ChiakiErrorCode err = start_discovery(NULL, NULL);
    if (err != CHIAKI_ERR_SUCCESS) {
      LOGD("Failed to start discovery: %d\n", err);
      sceKernelExitProcess(err);
    }
  }

  LOGD("Starting to draw UI");
  draw_ui();

  // Cleanup
  // Controller diagram now uses procedural rendering - no textures to free
  if (context.mlog) {
    free(context.mlog);
  }

  // Clean up finalization mutex
  chiaki_mutex_fini(&context.stream.finalization_mutex);
  LOGD("Finalization mutex destroyed");
}
