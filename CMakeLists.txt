# Dusklight "Chicken Control" mod
#
# This follows TwilitRealm/mod-template exactly (see cmake/FetchDusklight.cmake,
# copied from that project): it fetches the real Dusklight SDK and builds a
# loadable .dusk bundle with the official add_mod() CMake macro. There is no
# custom transport here — the mod uses Dusklight's own NetService for
# networking, LogService for logging, and (via FEATURES game) direct calls
# into the Twilight Princess decompilation Dusklight is built on.
#
#   cmake -B build
#   cmake --build build
#
# Produces build/mods/chicken_control_bridge.dusk. Copy that file into your
# Dusklight mods folder (see README.md) or drag it onto the Dusklight window.

cmake_minimum_required(VERSION 3.26)

if (NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type")
endif ()

project(chicken_control_bridge VERSION 1.0.0 LANGUAGES CXX)

# Pinned to the Dusklight release this mod was built and tested against
# (v2.0.1 for Windows x86_64, matching the user-reported install). Override
# with -DDUSKLIGHT_VERSION=... or -DDUSKLIGHT_DIR=... to build against a
# different checkout.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "v2.0.1")
endif ()

# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

# mod.json.in -> mod.json, same as mod-template (fills in @PROJECT_VERSION@).
configure_file(mod.json.in "${CMAKE_CURRENT_BINARY_DIR}/mod.json" @ONLY NEWLINE_STYLE LF)

add_mod(chicken_control_bridge
        FEATURES game            # calls straight into the TP decomp (game_api.cpp)
        SOURCES src/mod.cpp
                src/net_bridge.cpp
                src/effects.cpp
                src/game_api.cpp
        MOD_JSON "${CMAKE_CURRENT_BINARY_DIR}/mod.json"
        RES_DIR res
        OVERLAY_DIR overlay
        TEXTURES_DIR textures
)

# game_api.cpp supports being compiled without the game (see its dual-mode
# comment) for the standalone unit test in tests/. When built as part of the
# real mod above, always compile the real calls in.
target_compile_definitions(chicken_control_bridge PRIVATE CHICKEN_CONTROL_HAS_GAME)
