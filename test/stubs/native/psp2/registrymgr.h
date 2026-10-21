/*
 * Native stand-in for the VitaSDK header <psp2/registrymgr.h>.
 *
 * Only what vita/src/config_values.c uses. The test (config_vita_tests.c) defines the function.
 * test/CMakeLists.txt adds this directory to the include path for native builds only, so the real
 * SDK header is used in the Vita cross-compile.
 */
#ifndef VITARPS5_TEST_STUB_PSP2_REGISTRYMGR_H
#define VITARPS5_TEST_STUB_PSP2_REGISTRYMGR_H

int sceRegMgrGetKeyInt(const char *category, const char *name, int *out_value);

#endif
