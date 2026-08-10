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
