#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>

void _start() __attribute__((weak, alias("module_start")));

int kuCtrlPeekPsButton(void) {
  uint32_t state;
  ENTER_SYSCALL(state);

  SceCtrlData pad;
  int res = ksceCtrlPeekBufferPositive(0, &pad, 1);

  EXIT_SYSCALL(state);
  if (res < 0)
    return res;
  return (pad.buttons & SCE_CTRL_PSBUTTON) ? 1 : 0;
}

int kuCtrlSetPsButtonMask(int enabled) {
  if (enabled) {
    return ksceCtrlUpdateMaskForAll(0, SCE_CTRL_PSBUTTON);
  }
  return ksceCtrlUpdateMaskForAll(SCE_CTRL_PSBUTTON, 0);
}

int module_start(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  // Best-effort cleanup so PS stays usable if plugin unloads.
  ksceCtrlUpdateMaskForAll(SCE_CTRL_PSBUTTON, 0);
  return SCE_KERNEL_STOP_SUCCESS;
}
