include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaRPS5")
set(VITA_TITLEID  "VRPS50001")  # VitaRPS5 unique ID (changed from CHIAKI001 to force LiveArea refresh)
# TODO: Make it dynamic
set(VITA_VERSION "00.06")

add_executable(${VITA_APP_NAME}.elf
    src/config.c
    src/context.c
    src/discovery.c
    src/main.c
    src/ui.c
    src/util.c
    src/host.c
    src/host_input.c
    src/host_disconnect.c
    src/host_feedback.c
    src/host_loss_profile.c
    src/host_metrics.c
    src/host_recovery.c
    src/host_storage.c
    src/controller.c
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
    src/ui/ui_controller_diagram.c

    third_party/tomlc99/toml.c
    third_party/h264-bitstream/h264_nal.c
	third_party/h264-bitstream/h264_sei.c
	third_party/h264-bitstream/h264_stream.c
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

if(DEFINED VITARPS5_DEBUG_MENU)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_DEBUG_MENU=${VITARPS5_DEBUG_MENU})
endif()

# Security: Control whether runtime TOML can override compiled logging settings
if(DEFINED VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG)
    target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG=${VITARPS5_ALLOW_RUNTIME_LOGGING_CONFIG})
endif()

target_include_directories(${VITA_APP_NAME}.elf PRIVATE
    include
    third_party
    third_party/h264-bitstream/
)

target_link_libraries(${VITA_APP_NAME}.elf
  # TODO: Do we really need all of those?
  chiaki-lib
  vita2d
  ssl
  crypto
  opus
  curl

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
        FILE res/assets/ps4_off.png assets/ps4_off.png
        FILE res/assets/ps4.png assets/ps4.png
        FILE res/assets/ps4_rest.png assets/ps4_rest.png
        FILE res/assets/ps5_off.png assets/ps5_off.png
        FILE res/assets/ps5.png assets/ps5.png
        FILE res/assets/ps5_rest.png assets/ps5_rest.png
        FILE res/assets/controller_front.png assets/controller_front.png
        FILE res/assets/controller_back.png assets/controller_back.png
        FILE res/assets/fonts/Roboto-Regular.ttf assets/fonts/Roboto-Regular.ttf
        FILE res/assets/fonts/RobotoMono-Regular.ttf assets/fonts/RobotoMono-Regular.ttf
        # VitaRPS5 UI Assets - all consolidated in assets/ directory
        FILE assets/symbol_triangle.png assets/symbol_triangle.png
        FILE assets/symbol_circle.png assets/symbol_circle.png
        FILE assets/symbol_ex.png assets/symbol_ex.png
        FILE assets/symbol_square.png assets/symbol_square.png
        FILE assets/wave_top.png assets/wave_top.png
        FILE assets/wave_bottom.png assets/wave_bottom.png
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
        # Professional UI assets (gradient background, logo, controller diagrams)
        FILE assets/background.png assets/background.png
        FILE assets/Vita_RPS5_Logo.png assets/Vita_RPS5_Logo.png
        FILE assets/Vita_Front.png assets/Vita_Front.png
        FILE assets/PS5_logo.png assets/PS5_logo.png
        # Controller layout screen now uses procedural rendering - PNG outlines removed
    )
endif()
