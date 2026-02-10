add_executable(vitarps5_tests
    config_vita_tests.c
    ../vita/src/config.c
    ../vita/third_party/tomlc99/toml.c
)

target_include_directories(vitarps5_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/vita/include
    ${CMAKE_SOURCE_DIR}/vita/third_party
    ${CMAKE_SOURCE_DIR}/lib/include
)

target_compile_definitions(vitarps5_tests PRIVATE
    CFG_FILENAME="${CMAKE_CURRENT_BINARY_DIR}/config_test.toml"
)

add_test(NAME vitarps5_config_tests COMMAND vitarps5_tests)
