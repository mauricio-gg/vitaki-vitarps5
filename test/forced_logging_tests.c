/* Beta builds compile with VITARPS5_FORCE_LOGGING=1 (.env.beta). This target reuses the stubs of
 * config_vita_tests.c (its own main() is renamed away) and checks the one behaviour that differs:
 * a saved enable_logging = false must not turn logging off. Native run: ./tools/native_tests.sh */
#define main config_vita_suite_main
#include "config_vita_tests.c"
#undef main

static void test_forced_logging_overrides_saved_disable(void) {
  reset_config_file();
  write_config_text("[general]\nversion = 1\n\n[settings]\nenable_logging = false\n");

  VitaChiakiConfig cfg;
  init_cfg(&cfg);

  assert(cfg.logging.enabled);
}

int main(void) {
  test_forced_logging_overrides_saved_disable();
  reset_config_file();
  puts("vitarps5 forced logging tests passed");
  return 0;
}
