cmake_minimum_required(VERSION 3.20)
project(MinecraftHeadTracking LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# pixi.toml is the single source of truth for the version; the bootstrap log
# prints what is parsed here, so crash reports quote a real artifact rather
# than a hand-maintained define that can drift.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/pixi.toml" _pixi_toml)
if(_pixi_toml MATCHES "(^|\n)version[ \t]*=[ \t]*\"([^\"]+)\"")
    set(MCHT_MOD_VERSION "${CMAKE_MATCH_2}")
else()
    message(FATAL_ERROR "Could not parse version from pixi.toml")
endif()

# Captured at configure time so it does not bust incremental builds. CI
# reconfigures on every push, so published artifacts always carry an accurate
# SHA; a local dev build may show a stale one until the next configure.
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE MCHT_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_sha_rc
    )
    if(NOT _git_sha_rc EQUAL 0)
        set(MCHT_GIT_SHA "unknown")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _git_dirty STREQUAL "")
        set(MCHT_GIT_SHA "${MCHT_GIT_SHA}-dirty")
    endif()
else()
    set(MCHT_GIT_SHA "unknown")
endif()
message(STATUS "MinecraftHeadTracking version: ${MCHT_MOD_VERSION} (${MCHT_GIT_SHA})")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

include(FetchContent)
# Pinned to the commit, not the tag. A tag is a movable ref: upstream (or
# anyone who reaches the account) can retarget v1.3.3 and every clean build
# from then on links different code into a DLL whose whole job is patching
# another process's memory. The SHA below is what v1.3.3 pointed at.
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3
)
FetchContent_MakeAvailable(minhook_src)

add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_DISCOVERY ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp)

# ---------------------------------------------------------------------------
# The mod itself: a DLL injected into Minecraft.Windows.exe.
#
# Bedrock installs under C:\Program Files\WindowsApps, which is owned by
# TrustedInstaller and whose package signature Windows validates, so the proxy
# DLL every other mod in this catalogue uses cannot be dropped beside the game
# EXE. Bedrock is however a full-trust packaged desktop app (its manifest
# declares Windows.FullTrustApplication + runFullTrust), not a sandboxed
# AppContainer, so ordinary same-user injection reaches it.
# ---------------------------------------------------------------------------
set(MOD_NAME MinecraftHeadTracking)

add_library(${MOD_NAME} SHARED
    src/MinecraftHeadTracking/dllmain.cpp
    src/MinecraftHeadTracking/discovery.cpp
    src/MinecraftHeadTracking/camera_hook.cpp
    src/MinecraftHeadTracking/head_tracking.cpp
    src/MinecraftHeadTracking/tracking_settings.cpp
    src/MinecraftHeadTracking/session_policy.cpp
    src/MinecraftHeadTracking/builds/build_registry.cpp
    src/MinecraftHeadTracking/builds/store_offsets.cpp
    src/MinecraftHeadTracking/builds/image_scan.cpp
    src/MinecraftHeadTracking/builds/code_resolver.cpp
)

target_include_directories(${MOD_NAME} PRIVATE src src/MinecraftHeadTracking)

target_compile_definitions(${MOD_NAME} PRIVATE
    MCHT_MOD_VERSION="${MCHT_MOD_VERSION}"
    MCHT_GIT_SHA="${MCHT_GIT_SHA}"
)

if(MSVC)
    target_compile_options(${MOD_NAME} PRIVATE /W4 /WX)
endif()

target_link_libraries(${MOD_NAME} PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
    psapi
)

set_target_properties(${MOD_NAME} PROPERTIES SUFFIX ".dll")

# ---------------------------------------------------------------------------
# The launcher: activates the Store package and injects the mod DLL. This is
# the loader for this game, standing in for the ASI/BepInEx layer elsewhere.
# ---------------------------------------------------------------------------
set(LAUNCHER_NAME MinecraftHeadTrackingLauncher)

add_executable(${LAUNCHER_NAME} src/Launcher/launcher.cpp)

target_include_directories(${LAUNCHER_NAME} PRIVATE src)

target_compile_definitions(${LAUNCHER_NAME} PRIVATE
    MCHT_MOD_VERSION="${MCHT_MOD_VERSION}"
    MCHT_GIT_SHA="${MCHT_GIT_SHA}"
)

if(MSVC)
    target_compile_options(${LAUNCHER_NAME} PRIVATE /W4 /WX)
endif()

target_link_libraries(${LAUNCHER_NAME} PRIVATE ole32 shell32 psapi)

# ---------------------------------------------------------------------------
# Tests for what runs without a game attached: the range checks on ini and
# command-line values, the frame clock, the pose and view composition the
# camera hook and the crosshair share, the crosshair projection, the held-pose
# ease-out, and the pinned per-build addresses. Built with the mod so
# `pixi run build` covers them; the submodule's own suite is a separate
# configure (`pixi run test`).
#
# store_offsets.cpp is compiled in rather than linked from the mod: it is the
# one file whose contents cannot be re-derived from anything else in the tree,
# so the suite pins every value in it. It pulls in no Windows or game state, so
# the submodule's headers are enough and nothing needs linking.
# ---------------------------------------------------------------------------
add_executable(mcht_tests
    tests/mcht_tests.cpp
    src/MinecraftHeadTracking/builds/store_offsets.cpp
)

target_include_directories(mcht_tests PRIVATE
    src
    src/MinecraftHeadTracking
    cameraunlock-core/cpp/include
)

if(MSVC)
    target_compile_options(mcht_tests PRIVATE /W4 /WX)
endif()

enable_testing()
add_test(NAME mcht_tests COMMAND mcht_tests)
