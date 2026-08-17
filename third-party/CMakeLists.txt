
if(NOT CHIAKI_USE_SYSTEM_NANOPB)
	##################
	# nanopb
	##################

	add_definitions(-DPB_C99_STATIC_ASSERT) # Fix PB_STATIC_ASSERT on msvc without using C11 for now
	add_subdirectory(nanopb EXCLUDE_FROM_ALL)
	set(NANOPB_GENERATOR_PY "${CMAKE_CURRENT_SOURCE_DIR}/nanopb/generator/nanopb_generator.py" PARENT_SCOPE)
	add_library(nanopb INTERFACE)
	target_link_libraries(nanopb INTERFACE protobuf-nanopb-static)
	target_include_directories(nanopb INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nanopb")
	target_compile_definitions(nanopb INTERFACE -DPB_C99_STATIC_ASSERT) # see above
	add_library(Nanopb::nanopb ALIAS nanopb)
endif()

if(NOT CHIAKI_USE_SYSTEM_JERASURE)
	##################
	# gf-complete
	##################

	set(GF_COMPLETE_SOURCE
			gf-complete/src/gf.c
			gf-complete/src/gf_wgen.c
			gf-complete/src/gf_w4.c
			gf-complete/src/gf_w8.c
			gf-complete/src/gf_w16.c
			gf-complete/src/gf_w32.c
			gf-complete/src/gf_w64.c
			gf-complete/src/gf_w128.c
			gf-complete/src/gf_rand.c
			gf-complete/src/gf_general.c
			gf-complete/src/gf_cpu.c)

	# TODO: support NEON

	add_library(gf_complete STATIC ${GF_COMPLETE_SOURCE})
	target_include_directories(gf_complete PUBLIC gf-complete/include)

	##################
	# jerasure
	##################

	set(JERASURE_SOURCE
			jerasure/src/galois.c
			jerasure/src/jerasure.c
			jerasure/src/reed_sol.c
			jerasure/src/cauchy.c
			jerasure/src/liberation.c)

	add_library(jerasure STATIC ${JERASURE_SOURCE})
	target_include_directories(jerasure PUBLIC jerasure/include)
	target_link_libraries(jerasure gf_complete)

	add_library(Jerasure::Jerasure ALIAS jerasure)
endif()

if(NOT CHIAKI_USE_SYSTEM_CURL)
	##################
	# libcurl (with experimental WebSocket support enabled)
	##################

	# BUILD_CURL_EXE, not BUILD_EXE — the latter matches no curl option and
	# silently never worked, so curl's CLI tool was always built as collateral.
	# We only ever consume CURL::libcurl_static; the exe is dead weight and
	# fails to link anyway because VitaSDK's newlib has no pthread_rwlock_*
	# family, which the toolchain's OpenSSL 1.1.1 references.
	set(BUILD_CURL_EXE OFF)
	set(BUILD_SHARED_LIBS OFF)
	set(BUILD_STATIC_LIBS ON)
	set(CURL_DISABLE_INSTALL ON)
	set(HTTP_ONLY ON)
	set(ENABLE_WEBSOCKETS ON)
if(WIN32)
	set(USE_SSLEAY ON)
	set(CURL_USE_SCHANNEL ON)
endif()
	add_subdirectory(curl)
	# Exported library is CURL::libcurl_static

	# VitaSDK's OpenSSL 1.1.1 is built with `no-ui-console` (UI_OpenSSL is
	# compiled out of libcrypto's ui_openssl.o) but its installed
	# opensslconf.h fails to define OPENSSL_NO_UI_CONSOLE.  Without that
	# define, curl's vtls/openssl.c enables USE_OPENSSL_ENGINE and links
	# against the absent UI_OpenSSL.  Tell curl the truth about the library.
	# Only OpenSSL ENGINE support (PKCS#11 / hardware crypto / engine-loaded
	# client-cert keys) is lost, none of which the Vita client uses.
	if(CHIAKI_IS_VITA AND TARGET libcurl_static)
		target_compile_definitions(libcurl_static PRIVATE OPENSSL_NO_UI_CONSOLE)
	endif()
endif()

if(CHIAKI_ENABLE_STEAM_SHORTCUT)
    if(NOT CHIAKI_ENABLE_VITA)
        add_subdirectory(cpp-steam-tools)
    endif()
endif()
