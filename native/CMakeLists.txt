# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
cmake_minimum_required(VERSION 3.20)
project(LinuxXR VERSION 0.3.3 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# LinuxXR - native Unity XR provider (OpenXR + Vulkan) for the Linux Editor
#
# Targets:
#   linuxxr_core   static library without Unity dependency (OpenXR/Vulkan logic)
#   LinuxXR        libLinuxXR.so - the Unity plugin (core + Unity glue)
#   linuxxr-probe  command-line tool for testing the chain without Unity
#   Tests          (LINUXXR_BUILD_TESTS=ON)
#
# BUILD-time dependencies are headers only (libvulkan-dev, libopenxr-dev, Unity XR headers).
# At RUNTIME libopenxr_loader.so.1 and libvulkan.so.1 are loaded via dlopen.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(LINUXXR_BUILD_TESTS "Build tests" ON)
option(LINUXXR_BUILD_PROBE "Build linuxxr-probe" ON)
# Release builds: link libstdc++/libgcc statically into libLinuxXR.so so it only depends on glibc
# (built on Ubuntu 22.04 it then runs on glibc 2.35+ incl. the Unity Hub Flatpak runtime).
option(LINUXXR_STATIC_RUNTIME "Link libstdc++ and libgcc statically into libLinuxXR.so" OFF)
set(LINUXXR_UNITY_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity-xr-headers"
    CACHE PATH "Path to the Unity XR SDK headers (scripts/fetch-unity-headers.sh)")
set(LINUXXR_UNITY_PLUGIN_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../unity/Packages/com.tyroonis.xr.linuxopenxr/Runtime/Native~"
    CACHE PATH "Target folder libLinuxXR.so is copied to after the build")

find_path(OPENXR_INCLUDE_DIR openxr/openxr.h REQUIRED)
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h REQUIRED)
message(STATUS "OpenXR headers: ${OPENXR_INCLUDE_DIR}")
message(STATUS "Vulkan headers: ${VULKAN_INCLUDE_DIR}")

set(LINUXXR_WARNINGS -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter)

# --- Core -------------------------------------------------------------------
add_library(linuxxr_core STATIC
  src/core/log.cpp
  src/core/xr_api.cpp
  src/core/vk_api.cpp
  src/core/runtime_select.cpp
  src/core/vk_requirements.cpp
  src/core/vk_preinit.cpp
  src/core/xr_context.cpp
  src/core/xr_input.cpp
)
target_include_directories(linuxxr_core PUBLIC src ${OPENXR_INCLUDE_DIR} ${VULKAN_INCLUDE_DIR})
target_compile_definitions(linuxxr_core PUBLIC XR_NO_PROTOTYPES VK_NO_PROTOTYPES XR_USE_GRAPHICS_API_VULKAN
                                               XR_USE_TIMESPEC LINUXXR_VERSION="${PROJECT_VERSION}")
target_compile_options(linuxxr_core PRIVATE ${LINUXXR_WARNINGS})
target_link_libraries(linuxxr_core PUBLIC ${CMAKE_DL_LIBS} pthread)

# --- Unity plugin -----------------------------------------------------------
if(EXISTS "${LINUXXR_UNITY_HEADERS}/IUnityXRDisplay.h")
  add_library(LinuxXR SHARED
    src/unity/plugin_main.cpp
    src/unity/unity_log.cpp
    src/unity/preinit.cpp
    src/unity/display_provider.cpp
    src/unity/input_provider.cpp
    src/unity/native_api.cpp
    src/unity/session.cpp
  )
  target_include_directories(LinuxXR PRIVATE src ${LINUXXR_UNITY_HEADERS})
  target_compile_options(LinuxXR PRIVATE ${LINUXXR_WARNINGS})
  target_link_libraries(LinuxXR PRIVATE linuxxr_core)
  # Export only the Unity entry points
  set_target_properties(LinuxXR PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LINK_FLAGS "-Wl,--no-undefined -Wl,-z,defs")
  if(LINUXXR_STATIC_RUNTIME)
    target_link_options(LinuxXR PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL)
  endif()

  if(LINUXXR_UNITY_PLUGIN_DIR)
    add_custom_command(TARGET LinuxXR POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${LINUXXR_UNITY_PLUGIN_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:LinuxXR> "${LINUXXR_UNITY_PLUGIN_DIR}/"
      COMMENT "Copying libLinuxXR.so into the Unity package")
  endif()
else()
  message(WARNING "Unity XR headers missing (${LINUXXR_UNITY_HEADERS}) - libLinuxXR.so will NOT be built. "
                  "Run scripts/fetch-unity-headers.sh.")
endif()

# --- Probe ------------------------------------------------------------------
if(LINUXXR_BUILD_PROBE)
  add_executable(linuxxr-probe tools/probe/main.cpp tools/probe/probe_vulkan.cpp)
  target_compile_options(linuxxr-probe PRIVATE ${LINUXXR_WARNINGS})
  target_link_libraries(linuxxr-probe PRIVATE linuxxr_core)
endif()

# --- Tests ------------------------------------------------------------------
if(LINUXXR_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  enable_testing()
  add_subdirectory(tests)
endif()
