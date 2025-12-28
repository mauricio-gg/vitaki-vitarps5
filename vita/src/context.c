#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include "message_log.h"
#include "context.h"
#include "config.h"
#include "logging.h"
#include "ui.h"

VitaChiakiContext context;

void log_cb_debugnet(ChiakiLogLevel lvl, const char *msg, void *user) {
  (void)user;
  char line[800];
  sceClibSnprintf(line, sizeof(line), "[CHIAKI] %s\n", msg);
  sceClibPrintf("%s", line);
  vita_log_submit_line(lvl, line);
  if (!context.stream.is_streaming) {
      if (context.mlog) {
        write_message_log(context.mlog, line);
      }
    }
}

bool vita_chiaki_init_context() {
  config_parse(&context.config);

  vita_log_module_init(&context.config.logging);
  uint32_t log_mask = vita_logging_profile_mask(context.config.logging.profile);
  if (!log_mask)
    log_mask = CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING;
  chiaki_log_init(&(context.log), log_mask, &log_cb_debugnet, NULL);
  context.mlog = message_log_create();

  write_message_log(context.mlog, "----- Debug log start -----"); // debug

  // Initialize stream finalization mutex (prevents race condition in session cleanup)
  ChiakiErrorCode err = chiaki_mutex_init(&context.stream.finalization_mutex, false);
  if (err != CHIAKI_ERR_SUCCESS) {
    chiaki_log(&context.log, CHIAKI_LOG_ERROR, "Failed to initialize finalization mutex: %d", err);
    return false;
  }

  // add manual hosts to context
  update_context_hosts();

  // init ui to select a certain button
  context.ui_state.active_item = UI_MAIN_WIDGET_SETTINGS_BTN;
  context.ui_state.next_active_item = context.ui_state.active_item;

  return true;
}
