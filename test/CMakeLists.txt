add_executable(vitarps5_tests
    config_vita_tests.c
    token_crypto_tests.c
    packet_path_tests.c
    json_escape_tests.c
    ../vita/src/config.c
    ../vita/src/config_migration.c
    ../vita/src/config_values.c
    ../vita/src/config_hosts.c
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
