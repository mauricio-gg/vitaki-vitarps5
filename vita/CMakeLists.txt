include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaRPS5")
set(VITA_TITLEID  "VRPS50001")  # VitaRPS5 unique ID (changed from CHIAKI001 to force LiveArea refresh)
# TODO: Make it dynamic
set(VITA_VERSION "00.06")

add_executable(${VITA_APP_NAME}.elf
    src/config.c
    src/config_migration.c
    src/config_values.c
    src/config_hosts.c
    src/room_icons.c
    src/context.c
    src/discovery.c
    src/discovery_host.c
    src/main.c
    src/ui.c
    src/util.c
    src/host.c
    src/host_input.c
    src/host_disconnect.c
    src/host_feedback.c
    src/host_callbacks.c
    src/host_lifecycle.c
    src/host_loss_profile.c
    src/host_metrics.c
    src/host_quit.c
    src/host_registration.c
    src/host_recovery.c
    src/host_storage.c
    src/psn_auth.c
    src/vita_dns.c
    src/psn_remote.c
    src/token_crypto.c
    src/controller.c
    src/controller_metadata.c
    src/video.c
    src/video_overlay.c
    src/audio.c
    src/message_log.c
    src/logging.c

    # UI modules (Phase 2, 3, 4, 5, 6 & 7 refactoring)
    src/ui/ui_graphics.c
    src/ui/ui_animation.c
    src/ui/ui_input.c
    src/ui/ui_state.c
    src/ui/ui_components.c
    src/ui/ui_navigation.c
    src/ui/ui_console_cards.c
    src/ui/ui_screens.c
    src/ui/ui_focus.c
    src/ui/ui_qr.c
    src/ui/ui_controller_diagram.c
    src/ui/ui_text.c
    src/ui/ui_component.c
    src/ui/ui_console_status.c
    src/ui/ui_console_rows.c
    src/ui/ui_category_bar.c
    src/ui/ui_xmb_list.c
    src/ui/ui_home.c
    src/ui/ui_home_detail.c
    src/ui/ui_detail_panel.c
    src/ui/ui_text_wrap.c
    src/ui/ui_value_labels.c
    src/ui/ui_background.c
    src/ui/ui_shapes.c
    src/ui/ui_pill.c
    src/ui/ui_top_bar.c
    src/ui/ui_hint_row.c
    src/ui/ui_chrome_layout.c
    src/ui/ui_bake.c
    src/ui/ui_type_logo.c
    src/ui/ui_page_frame.c
    src/ui/ui_spinner.c
    src/ui/ui_connecting_ring.c
    src/ui/ui_steps.c
    src/ui/ui_text_button.c
    src/ui/ui_connecting_flow.c
    src/ui/ui_connecting.c
    src/ui/ui_group_list.c
    src/ui/ui_setting_list.c
    src/ui/ui_scroll_indicator.c
    src/ui/ui_settings_actions.c
    src/ui/ui_settings.c
    src/ui/ui_reconnecting.c

    third_party/tomlc99/toml.c
    third_party/h264-bitstream/h264_nal.c
	third_party/h264-bitstream/h264_sei.c
	third_party/h264-bitstream/h264_stream.c
    third_party/qrcodegen/qrcodegen.c
)

if(CHIAKI_ENABLE_VITA_HOLEPUNCH)
    target_sources(${VITA_APP_NAME}.elf PRIVATE
        ../third-party/vita-stubs/posix_stubs.c)
endif()

# VitaRPS5: vendored libvita2d sources with LINEAR atlas filter patch.
# These object files are linked directly so they shadow vita2d_font.o and
# texture_atlas.o inside the system -lvita2d archive. All other vita2d
# symbols still come from -lvita2d below.
target_sources(${VITA_APP_NAME}.elf PRIVATE
    ../third-party/libvita2d/vita2d_font.c
    ../third-party/libvita2d/texture_atlas.c
)

# Logging configuration is controlled via environment files (.env.prod, .env.testing)
# and passed to CMake via build.sh. No hardcoded defaults here to avoid redefinition warnings.

target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC
    CHIAKI_VERSION="${CHIAKI_VERSION}"
)
if(DEFINED VITARPS5_DEFAULT_LOG_PROFILE)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_DEFAULT_LOG_PROFILE=${VITARPS5_DEFAULT_LOG_PROFILE})
endif()
if(DEFINED VITARPS5_LOGGING_DEFAULT_ENABLED)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_LOGGING_DEFAULT_ENABLED=${VITARPS5_LOGGING_DEFAULT_ENABLED})
endif()
if(DEFINED VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS=${VITARPS5_LOGGING_DEFAULT_FORCE_ERRORS})
endif()
if(DEFINED VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH=${VITARPS5_LOGGING_DEFAULT_QUEUE_DEPTH})
endif()
if(DEFINED VITARPS5_LOGGING_DEFAULT_PATH)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_LOGGING_DEFAULT_PATH="${VITARPS5_LOGGING_DEFAULT_PATH}")
endif()
if(DEFINED VITARPS5_BUILD_GIT_COMMIT)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_BUILD_GIT_COMMIT="${VITARPS5_BUILD_GIT_COMMIT}")
endif()
if(DEFINED VITARPS5_BUILD_GIT_BRANCH)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_BUILD_GIT_BRANCH="${VITARPS5_BUILD_GIT_BRANCH}")
endif()
if(DEFINED VITARPS5_BUILD_GIT_DIRTY)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_BUILD_GIT_DIRTY=${VITARPS5_BUILD_GIT_DIRTY})
endif()
if(DEFINED VITARPS5_BUILD_TIMESTAMP)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_BUILD_TIMESTAMP="${VITARPS5_BUILD_TIMESTAMP}")
endif()
if(DEFINED VITARPS5_PSN_OAUTH_DEVICE_CODE_URL)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_DEVICE_CODE_URL="${VITARPS5_PSN_OAUTH_DEVICE_CODE_URL}")
endif()
if(DEFINED VITARPS5_PSN_OAUTH_AUTHORIZE_URL)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_AUTHORIZE_URL="${VITARPS5_PSN_OAUTH_AUTHORIZE_URL}")
endif()
if(DEFINED VITARPS5_PSN_OAUTH_TOKEN_URL)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_TOKEN_URL="${VITARPS5_PSN_OAUTH_TOKEN_URL}")
endif()
if(NOT DEFINED VITARPS5_PSN_OAUTH_CLIENT_ID OR "${VITARPS5_PSN_OAUTH_CLIENT_ID}" STREQUAL "")
    message(FATAL_ERROR "VITARPS5_PSN_OAUTH_CLIENT_ID is required. Provide via .env.prod or .env.testing.")
endif()
if(NOT DEFINED VITARPS5_PSN_OAUTH_CLIENT_SECRET OR "${VITARPS5_PSN_OAUTH_CLIENT_SECRET}" STREQUAL "")
    message(FATAL_ERROR "VITARPS5_PSN_OAUTH_CLIENT_SECRET is required. Provide via .env.prod or .env.testing.")
endif()
target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_CLIENT_ID="${VITARPS5_PSN_OAUTH_CLIENT_ID}")
target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_CLIENT_SECRET="${VITARPS5_PSN_OAUTH_CLIENT_SECRET}")
if(DEFINED VITARPS5_PSN_OAUTH_SCOPE)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_SCOPE="${VITARPS5_PSN_OAUTH_SCOPE}")
endif()
if(DEFINED VITARPS5_PSN_OAUTH_REDIRECT_URI)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PSN_OAUTH_REDIRECT_URI="${VITARPS5_PSN_OAUTH_REDIRECT_URI}")
endif()

if(DEFINED VITARPS5_DEBUG_MENU)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_DEBUG_MENU=${VITARPS5_DEBUG_MENU})
endif()

# GH #275: debug-only stream tools (resync widget). tools/build.sh always passes an explicit 0/1.
if(DEFINED VITARPS5_DEBUG_TOOLS)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_DEBUG_TOOLS=${VITARPS5_DEBUG_TOOLS})
    if(VITARPS5_DEBUG_TOOLS)
        target_sources(${VITA_APP_NAME}.elf PRIVATE src/debug_tools.c src/ui/ui_draw_stats.c)
        # Ticket #300: count every sceGxmDraw (ui_draw_stats.c). Testing builds only.
        target_link_options(${VITA_APP_NAME}.elf PRIVATE -Wl,--wrap=sceGxmDraw)
    endif()
endif()

# Security: Gate plaintext PSN OAuth token persistence. Default OFF so production builds
# never silently write tokens to disk. Enable only for local/QA debugging until encrypted
# storage lands (see GitHub issue #81).
option(VITARPS5_PLAINTEXT_TOKEN_STORAGE "Persist PSN OAuth tokens in plaintext config. INSECURE — enable only for local debugging until token encryption (#81) lands." OFF)
if(VITARPS5_PLAINTEXT_TOKEN_STORAGE)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_PLAINTEXT_TOKEN_STORAGE=1)
endif()

# Security: Control whether runtime TOML can override compiled logging settings
if(DEFINED VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG=${VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG})
endif()

target_include_directories(${VITA_APP_NAME}.elf PRIVATE
    include
    third_party
    third_party/h264-bitstream/
    ../third-party/libvita2d/include
    $ENV{VITASDK}/arm-vita-eabi/include/freetype2
)

if(CHIAKI_USE_SYSTEM_CURL)
    set(VITA_CURL_TARGET curl)
else()
    set(VITA_CURL_TARGET CURL::libcurl_static)
endif()

target_link_libraries(${VITA_APP_NAME}.elf
  # TODO: Do we really need all of those?
  chiaki-lib
  vita2d
  ssl
  crypto
  opus
  ${VITA_CURL_TARGET}

  freetype
  bz2
  png
  jpeg
  z
  m
  c
  -Wl,--whole-archive
  pthread
  -Wl,--no-whole-archive
  zip

  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceRegistryMgr_stub

  SceMotion_stub
  SceTouch_stub
#   SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  ScePower_stub
  SceVideodec_stub
  SceCodecEngine_stub
  SceAudio_stub
  SceIme_stub
  SceSsl_stub
  ScePower_stub
  SceLibKernel_stub
)


if(CHIAKI_IS_VITA)
    vita_create_self(${VITA_APP_NAME}.self ${VITA_APP_NAME}.elf UNSAFE)

    vita_create_vpk(${VITA_APP_NAME}.vpk ${VITA_TITLEID} ${VITA_APP_NAME}.self
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        # Livearea stuff
        FILE res/icon0.png sce_sys/icon0.png
        FILE res/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
        FILE res/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE res/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        # Textures, other assets
        FILE res/assets/ps4.png assets/ps4.png
        FILE res/assets/controller_front.png assets/controller_front.png
        FILE res/assets/controller_back.png assets/controller_back.png
        FILE res/assets/fonts/Roboto-Regular.ttf assets/fonts/Roboto-Regular.ttf
        FILE res/assets/fonts/Roboto-Light.ttf assets/fonts/Roboto-Light.ttf
        FILE res/assets/fonts/RobotoMono-Regular.ttf assets/fonts/RobotoMono-Regular.ttf
        # VitaRPS5 UI Assets - all consolidated in assets/ directory
        FILE assets/symbol_triangle.png assets/symbol_triangle.png
        FILE assets/symbol_circle.png assets/symbol_circle.png
        FILE assets/symbol_ex.png assets/symbol_ex.png
        FILE assets/symbol_square.png assets/symbol_square.png
        FILE assets/ellipse_green.png assets/ellipse_green.png
        FILE assets/ellipse_yellow.png assets/ellipse_yellow.png
        FILE assets/ellipse_red.png assets/ellipse_red.png
        FILE assets/button_add_new.png assets/button_add_new.png
        # Navigation Icons
        FILE assets/icon_settings.png assets/icon_settings.png
        FILE assets/icon_controller.png assets/icon_controller.png
        FILE assets/icon_profile.png assets/icon_profile.png
        FILE assets/icon_play.png assets/icon_play.png
        FILE assets/icon_button_triangle.png assets/icon_button_triangle.png
        # XMB UI icons: category icons 48 px, item icons 38 px (flat white)
        FILE assets/icons/controller.png assets/icons/controller.png
        FILE assets/icons/profile.png assets/icons/profile.png
        FILE assets/icons/tv.png assets/icons/tv.png
        FILE assets/icons/video.png assets/icons/video.png
        FILE assets/icons/network.png assets/icons/network.png
        FILE assets/icons/display.png assets/icons/display.png
        FILE assets/icons/controls.png assets/icons/controls.png
        FILE assets/icons/advanced.png assets/icons/advanced.png
        FILE assets/icons/account.png assets/icons/account.png
        FILE assets/icons/connection.png assets/icons/connection.png
        FILE assets/icons/psn.png assets/icons/psn.png
        FILE assets/icons/slot1.png assets/icons/slot1.png
        FILE assets/icons/slot2.png assets/icons/slot2.png
        FILE assets/icons/slot3.png assets/icons/slot3.png
        FILE assets/icons/search.png assets/icons/search.png
        FILE assets/icons/wifi.png assets/icons/wifi.png
        FILE assets/icons/battery.png assets/icons/battery.png
        # Connecting screen: 32 px page-title icons and the 64 px room icon inside the ring
        FILE assets/icons/page_lan.png assets/icons/page_lan.png
        FILE assets/icons/page_globe.png assets/icons/page_globe.png
        FILE assets/icons/page_moon.png assets/icons/page_moon.png
        FILE assets/icons/page_wifi.png assets/icons/page_wifi.png
        FILE assets/icons/page_gear.png assets/icons/page_gear.png
        FILE assets/icons/tv_64.png assets/icons/tv_64.png
        FILE assets/icons/room_tv_38.png assets/icons/room_tv_38.png
        FILE assets/icons/room_tv_48.png assets/icons/room_tv_48.png
        FILE assets/icons/room_tv_64.png assets/icons/room_tv_64.png
        FILE assets/icons/room_sofa_38.png assets/icons/room_sofa_38.png
        FILE assets/icons/room_sofa_48.png assets/icons/room_sofa_48.png
        FILE assets/icons/room_sofa_64.png assets/icons/room_sofa_64.png
        FILE assets/icons/room_bed_38.png assets/icons/room_bed_38.png
        FILE assets/icons/room_bed_48.png assets/icons/room_bed_48.png
        FILE assets/icons/room_bed_64.png assets/icons/room_bed_64.png
        FILE assets/icons/room_bunk_38.png assets/icons/room_bunk_38.png
        FILE assets/icons/room_bunk_48.png assets/icons/room_bunk_48.png
        FILE assets/icons/room_bunk_64.png assets/icons/room_bunk_64.png
        FILE assets/icons/room_desk_38.png assets/icons/room_desk_38.png
        FILE assets/icons/room_desk_48.png assets/icons/room_desk_48.png
        FILE assets/icons/room_desk_64.png assets/icons/room_desk_64.png
        FILE assets/icons/room_house_38.png assets/icons/room_house_38.png
        FILE assets/icons/room_house_48.png assets/icons/room_house_48.png
        FILE assets/icons/room_house_64.png assets/icons/room_house_64.png
        FILE assets/glyphs/hint_l.png assets/glyphs/hint_l.png
        FILE assets/glyphs/hint_r.png assets/glyphs/hint_r.png
        # Professional UI assets (gradient background, logo, controller diagrams)
        FILE assets/background.png assets/background.png
        FILE assets/Vita_RPS5_Logo.png assets/Vita_RPS5_Logo.png
        FILE assets/PS5_logo.png assets/PS5_logo.png
        FILE ../assets/psn-ca-bundle.pem assets/psn-ca-bundle.pem
        # Controller layout screen now uses procedural rendering - PNG outlines removed
    )
endif()
