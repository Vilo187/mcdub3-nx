cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Midnight Club 3: DUB Edition (Xbox) - Static Recompilation
#
# Hosts: Linux (GCC, SDL2 + OpenGL or Vulkan; the day-to-day debug build) and
# Nintendo Switch homebrew (devkitA64 + libnx, built with switch/build.sh).
# Layout and workflow: README.md, CLAUDE.md, docs/decisions/.
# ============================================================================

project(mc3_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xboxrecomp" CACHE PATH
    "xboxrecomp toolkit (vendored; see docs/decisions/0002)")

# The lifted C (~100 MB) is generated from the retail XBE by tools/regen.sh
# and never committed. Default matches tools/env.sh: <repo>/work.
if(DEFINED ENV{MC3_WORK})
    set(_mc3_work "$ENV{MC3_WORK}")
else()
    set(_mc3_work "${CMAKE_CURRENT_SOURCE_DIR}/work")
endif()
set(MC3_GEN_DIR "${_mc3_work}/gen" CACHE PATH
    "tools.recomp output (recomp_*.c, recomp_funcs.h, recomp_types.h)")
if(NOT EXISTS "${MC3_GEN_DIR}/recomp_funcs.h")
    message(FATAL_ERROR "No generated code in ${MC3_GEN_DIR}. Run tools/first_lift.sh "
                        "(or tools/regen.sh), or pass -DMC3_GEN_DIR=<dir>.")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/xbe_facts.h")
    message(FATAL_ERROR "config/xbe_facts.h is missing: run tools/first_lift.sh.")
endif()

# On the Switch: sdmc:/switch/<MC3_APP>/ for the game, logs, settings and caches.
set(MC3_APP "mcdub3" CACHE STRING "Application name and Switch data directory")
set(XBOXRECOMP_APP_NAME "${MC3_APP}" CACHE STRING "" FORCE)

# Vulkan (nv2a_vk; NVK on the Switch) instead of OpenGL (nv2a_gl).
option(MC3_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${MC3_VULKAN} CACHE BOOL "" FORCE)

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB MC3_GEN_SOURCES CONFIGURE_DEPENDS "${MC3_GEN_DIR}/*.c")

set(MC3_SOURCES
    src/main.c
    src/recomp_manual.c
    src/ac97_nabm.c
    src/difftest.c
    src/bink_native.c
)
if(NINTENDO_SWITCH)
    list(APPEND MC3_SOURCES src/switch_nx.c)
endif()

add_executable(${PROJECT_NAME} ${MC3_SOURCES} ${MC3_GEN_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)
target_compile_definitions(${PROJECT_NAME} PRIVATE MC3_APP="${MC3_APP}")
if(MC3_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MC3_VULKAN=1)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${MC3_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

# The generated chunks are huge straight-line functions. -O1 keeps GCC's time
# and memory per chunk sane on the PC; the Switch's CPU is the bottleneck, so
# it gets -O2. -fwrapv / no strict aliasing: x86 semantics, not C's.
if(NINTENDO_SWITCH)
    set(MC3_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
else()
    set(MC3_GEN_OPT "-O1" CACHE STRING "Optimisation for the lifted code")
endif()
set(MC3_GEN_FLAGS "${MC3_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv")
# Debug info for the lifted C is gigabytes per build tree. Off by default:
# backtraces and gdb breakpoints still name every sub_XXXXXXXX (symbols stay);
# turn it on to step through lifted C line by line.
option(MC3_GEN_DEBUGINFO "Debug info (-g) for the lifted code" OFF)
if(NOT MC3_GEN_DEBUGINFO)
    list(APPEND MC3_GEN_FLAGS "-g0")
endif()

# Link-time optimisation of the lifted code and the manual overrides, so lifted
# callers inline each other (~1-2 GB of RAM per LTO job).
option(MC3_LTO "Link-time optimisation of the lifted code" OFF)
set(MC3_LTO_JOBS "4" CACHE STRING "Parallel LTO link jobs")
if(MC3_LTO)
    list(APPEND MC3_GEN_FLAGS "-flto=auto")
    set_property(SOURCE src/recomp_manual.c APPEND PROPERTY
        COMPILE_OPTIONS "-flto=auto;-fno-strict-aliasing;-fwrapv")
    target_link_options(${PROJECT_NAME} PRIVATE
        "-flto=${MC3_LTO_JOBS}" "-flto-partition=balanced" ${MC3_GEN_OPT})
endif()
set_source_files_properties(${MC3_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "${MC3_GEN_FLAGS}")
target_link_libraries(${PROJECT_NAME} PRIVATE m)

# Movies: FFmpeg decodes Bink video in place of the lifted decoder
# (src/bink_native.c). Linux only for now; the Switch needs its own LGPL,
# Bink-only FFmpeg build. Without FFmpeg the lifted decoder plays the movies.
option(MC3_NATIVE_BINK "Decode Bink movie video with FFmpeg" ON)
if(MC3_NATIVE_BINK AND NOT NINTENDO_SWITCH)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(MC3_FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil)
    endif()
    if(MC3_FFMPEG_FOUND)
        target_compile_definitions(${PROJECT_NAME} PRIVATE MC3_NATIVE_BINK=1)
        target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::MC3_FFMPEG)
        # Movie sound plays on the runtime's software mixer (apu/apu.h).
        set_property(SOURCE src/bink_native.c APPEND PROPERTY
            INCLUDE_DIRECTORIES ${XBOXRECOMP_DIR}/src)
        message(STATUS "Bink video: FFmpeg ${MC3_FFMPEG_libavcodec_VERSION}")
    else()
        message(STATUS "Bink video: FFmpeg not found, lifted decoder")
    endif()
endif()

# ── Nintendo Switch homebrew (devkitA64 + libnx) ────────────────
if(NINTENDO_SWITCH)
    set(MC3_SWITCH_TITLE "Midnight Club 3 (Xbox recomp)" CACHE STRING "Title in hbmenu")
    set(MC3_SWITCH_AUTHOR "mcdub3-sw" CACHE STRING "Author in hbmenu")
    set(MC3_SWITCH_VERSION "0.0.1")
    set(MC3_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg" CACHE FILEPATH
        "256x256 JPEG icon (libnx default when missing)")

    if(MC3_VULKAN)
        target_link_libraries(${PROJECT_NAME} PRIVATE nx m)    # NVK comes with nv2a_vk
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau nx m)
    endif()

    set(MC3_NACP "${CMAKE_CURRENT_BINARY_DIR}/${MC3_APP}.nacp")
    nx_generate_nacp(OUTPUT "${MC3_NACP}"
        NAME "${MC3_SWITCH_TITLE}" AUTHOR "${MC3_SWITCH_AUTHOR}"
        VERSION "${MC3_SWITCH_VERSION}")
    if(EXISTS "${MC3_SWITCH_ICON}")
        nx_create_nro(${PROJECT_NAME} NACP "${MC3_NACP}" ICON "${MC3_SWITCH_ICON}")
    else()
        nx_create_nro(${PROJECT_NAME} NACP "${MC3_NACP}")
    endif()
endif()
