/**
 * @file ui_controller_rules.c
 * @brief Pure rules of the Controller page (see ui_controller_rules.h)
 */

#include "ui/ui_controller_rules.h"

#include <stddef.h>

int ui_controller_common_output(const int *outputs, int count) {
  if (!outputs || count <= 0)
    return UI_CTRL_MIXED;
  for (int i = 1; i < count; i++) {
    if (outputs[i] != outputs[0])
      return UI_CTRL_MIXED;
  }
  return outputs[0];
}
