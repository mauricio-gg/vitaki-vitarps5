add_executable(vitarps5_tests
    config_vita_tests.c
    token_crypto_tests.c
    packet_path_tests.c
    json_escape_tests.c
    ../vita/src/config.c
    ../vita/src/config_writer.c
    ../vita/src/config_migration.c
    ../vita/src/config_values.c
    ../vita/src/config_hosts.c
    ../vita/src/room_icons.c
    ../vita/src/token_crypto.c
    ../vita/third_party/tomlc99/toml.c
    ../lib/src/reorderqueue.c
    ../lib/src/videoreceiver_gap.c
    ../lib/src/base64.c
    ../lib/src/packetstats.c
    ../lib/src/thread.c
    ../lib/src/time.c
)

target_include_directories(vitarps5_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/vita/include
    ${CMAKE_SOURCE_DIR}/vita/third_party
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(vitarps5_tests PRIVATE
    CFG_FILENAME="${CMAKE_CURRENT_BINARY_DIR}/config_test.toml"
    VITARPS5_TEST_BUILD=1
)

# token_crypto.c uses OpenSSL EVP (AES-256-GCM, SHA-256, RAND_bytes).
# On the host build OpenSSL is always available through chiaki-lib's dependency.
find_package(OpenSSL REQUIRED)

# packetstats.c needs chiaki_mutex_* from thread.c, whose non-Vita/Switch/Windows
# path uses pthread. Link Threads explicitly for portability, even though it's
# already available implicitly via libSystem on this Darwin host.
find_package(Threads REQUIRED)

target_link_libraries(vitarps5_tests OpenSSL::Crypto Threads::Threads)

add_test(NAME vitarps5_config_tests COMMAND vitarps5_tests)

# bitstream_sps_tests.c has its own main() and #includes lib/src/bitstream.c
# directly (with a local chiaki_log() stub), so it is a standalone binary
# rather than another source file folded into vitarps5_tests above.
#
# NOTE: this project's Vita test harness (./tools/build.sh test) configures
# with the arm-vita-eabi toolchain but only ever runs `make vitarps5_tests`,
# so it cross-compiles this target (verifying it stays buildable) without
# executing it -- arm-vita-eabi ELFs can't run on the host. The authoritative
# run of these assertions is the native rerun documented at the top of
# bitstream_sps_tests.c: `cc -std=c99 -I lib/include -I lib/src -I
# test/stubs test/bitstream_sps_tests.c -o /tmp/bitstream_sps_tests`.
add_executable(bitstream_sps_tests bitstream_sps_tests.c)

target_include_directories(bitstream_sps_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

add_test(NAME vitarps5_bitstream_sps_tests COMMAND bitstream_sps_tests)

# ui_chrome_layout_tests.c checks the pure hint-row collapse and banner ellipsis rules.
# Standalone binary with its own main(); run natively (see the file header).
add_executable(ui_chrome_layout_tests
    ui_chrome_layout_tests.c
    ../vita/src/ui/ui_chrome_layout.c
)

target_include_directories(ui_chrome_layout_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_chrome_layout_tests COMMAND ui_chrome_layout_tests)

# ui_detail_tests.c has its own main() and tests the pure XMB helpers (status-message word wrap,
# ease-out curve). Like bitstream_sps_tests it is cross-compiled but never run by
# ./tools/build.sh test; the native command is at the top of the file.
add_executable(ui_detail_tests ui_detail_tests.c ../vita/src/ui/ui_text_wrap.c)

target_include_directories(ui_detail_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_detail_tests COMMAND ui_detail_tests)

# ui_zone_select_tests.c has its own main() and tests the pure ZoneGrid selection rules (cell from
# point, finger-paint backtrack). Cross-compiled but never run by ./tools/build.sh test; the native
# command is at the top of the file.
add_executable(ui_zone_select_tests ui_zone_select_tests.c ../vita/src/ui/ui_zone_select.c)

target_include_directories(ui_zone_select_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_zone_select_tests COMMAND ui_zone_select_tests)

# ui_connecting_flow_tests.c has its own main() and tests the pure Connecting screen rules (flow,
# steps, title). Cross-compiled but never run by ./tools/build.sh test; the native command is at
# the top of the file.
add_executable(ui_connecting_flow_tests
    ui_connecting_flow_tests.c
    ../vita/src/ui/ui_connecting_flow.c
)

target_include_directories(ui_connecting_flow_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_connecting_flow_tests COMMAND ui_connecting_flow_tests)

# ui_console_status_tests.c checks the pure Error / Retrying message classification and the
# row status precedence. Standalone binary with its own main(); run natively (see the file header).
add_executable(ui_console_status_tests
    ui_console_status_tests.c
    ../vita/src/ui/ui_console_status.c
    ../vita/src/ip_address.c
)

target_include_directories(ui_console_status_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_console_status_tests COMMAND ui_console_status_tests)

# pairing_result_tests.c checks the pairing result decision and the result popup copy.
# Standalone binary with its own main(); run natively (see the file header).
add_executable(pairing_result_tests
    pairing_result_tests.c
    ../vita/src/host_registration_result.c
    ../vita/src/ui/ui_result_copy.c
)

target_include_directories(pairing_result_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_pairing_result_tests COMMAND pairing_result_tests)

# pin_digits_tests.c checks the pure PIN rules (digit stepping, full check, value with leading
# zeros). Standalone binary with its own main(); run natively (see the file header).
add_executable(pin_digits_tests
    pin_digits_tests.c
    ../vita/src/ui/ui_pin_digits.c
)

target_include_directories(pin_digits_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_pin_digits_tests COMMAND pin_digits_tests)

# ui_console_rows_tests.c checks the pure Consoles list rules (Filter row, row-to-console
# mapping, focus, filter match). Standalone binary with its own main(); run natively (see the
# file header).
add_executable(ui_console_rows_tests
    ui_console_rows_tests.c
    ../vita/src/ui/ui_console_rows.c
)

target_include_directories(ui_console_rows_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_console_rows_tests COMMAND ui_console_rows_tests)

# ip_address_tests.c checks the typed-IP check used by Pair new device > Enter IP address.
# Standalone binary with its own main(); run natively (see the file header).
add_executable(ip_address_tests
    ip_address_tests.c
    ../vita/src/ip_address.c
)

target_include_directories(ip_address_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ip_address_tests COMMAND ip_address_tests)

# ui_controller_rules_tests.c checks the pure Controller page rules (what touching a zone does,
# the whole-side value or Mixed, assignments) against the real first-run seeds of controller.c.
# Standalone binary with its own main(); run natively (see the file header).
add_executable(ui_controller_rules_tests
    ui_controller_rules_tests.c
    ../vita/src/ui/ui_controller_rules.c
    ../vita/src/controller.c
)

target_include_directories(ui_controller_rules_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/vita/include
    ${CMAKE_SOURCE_DIR}/lib/include
)

add_test(NAME vitarps5_ui_controller_rules_tests COMMAND ui_controller_rules_tests)

# ui_downsample_tests.c checks the pure 2 x 2 pixel averaging behind the popup background freeze.
# Standalone binary with its own main(); run natively (see the file header).
add_executable(ui_downsample_tests
    ui_downsample_tests.c
    ../vita/src/ui/ui_downsample.c
)

target_include_directories(ui_downsample_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_downsample_tests COMMAND ui_downsample_tests)

# ui_stream_stats_tests.c checks the Latency and FPS value rules of the stream stats panel.
# Standalone binary with its own main(); run natively (see the file header).
add_executable(ui_stream_stats_tests
    ui_stream_stats_tests.c
    ../vita/src/ui/ui_stream_stats.c
)

target_include_directories(ui_stream_stats_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_stream_stats_tests COMMAND ui_stream_stats_tests)

# ui_login_tests.c has its own main() and tests the phone login helpers (QR module size, keyboard
# text conversion). Cross-compiled but never run by ./tools/build.sh test; the native command is at
# the top of the file.
add_executable(ui_login_tests ui_login_tests.c ../vita/src/ui/ui_utf16.c)

target_include_directories(ui_login_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_login_tests COMMAND ui_login_tests)

# ui_gesture_tests.c has its own main() and tests the pure tap / long-press / swipe rules.
# Cross-compiled but never run by ./tools/build.sh test; the native command is at the top of the file.
add_executable(ui_gesture_tests ui_gesture_tests.c ../vita/src/ui/ui_gesture.c)

target_include_directories(ui_gesture_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_ui_gesture_tests COMMAND ui_gesture_tests)

# psn_auth_rules_tests.c has its own main() and tests the pure PSN auth decision rules (refresh
# refused during a phone login, errors keep a login pending, a rejected refresh token is not
# retried). Cross-compiled but never run by ./tools/build.sh test; the native command is at the
# top of the file.
add_executable(psn_auth_rules_tests psn_auth_rules_tests.c ../vita/src/psn_auth_rules.c)

target_include_directories(psn_auth_rules_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

add_test(NAME vitarps5_psn_auth_rules_tests COMMAND psn_auth_rules_tests)

# redact_tests.c has its own main() and tests lib/src/redact.c and the redacted hexdump (credentials in log output keep
# only their first 4 characters, GH #361). ./tools/build.sh test neither builds nor runs it; the
# native cc command at the top of redact_tests.c is how it is run.
add_executable(redact_tests redact_tests.c ../lib/src/redact.c ../lib/src/log.c)

target_include_directories(redact_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/include
)

add_test(NAME vitarps5_redact_tests COMMAND redact_tests)

# ui_splash_particles_tests.c checks the splash particle sampler: targets only on bright opaque
# logo pixels, the row stride, the output capacity. Standalone binary with its own main(); run
# natively (see the file header).
add_executable(ui_splash_particles_tests
    ui_splash_particles_tests.c
    ../vita/src/ui/ui_splash_particles.c
)

target_include_directories(ui_splash_particles_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/vita/include
)

target_link_libraries(ui_splash_particles_tests m)

add_test(NAME vitarps5_ui_splash_particles_tests COMMAND ui_splash_particles_tests)

# config_writer_tests.c checks the config writer's ordering and its failure reporting. Standalone
# binary with its own main(); run natively (see the file header).
add_executable(config_writer_tests
    config_writer_tests.c
    ../vita/src/config_writer.c
    ../lib/src/thread.c
    ../lib/src/time.c
)

target_include_directories(config_writer_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/vita/include
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_compile_definitions(config_writer_tests PRIVATE
    CFG_FILENAME="${CMAKE_CURRENT_BINARY_DIR}/config_writer_test.toml"
)

target_link_libraries(config_writer_tests Threads::Threads)

add_test(NAME vitarps5_config_writer_tests COMMAND config_writer_tests)
