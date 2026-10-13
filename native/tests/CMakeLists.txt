# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tyroonis
# Tests for LinuxXR - no external framework (see check.h).
# Included from the main CMakeLists via add_subdirectory (LINUXXR_BUILD_TESTS=ON).

set(LINUXXR_TEST_WARNINGS -Wall -Wextra)

# --- Unit tests for linuxxr_core -------------------------------------------------------------
add_executable(linuxxr_core_tests test_core.cpp)
target_compile_options(linuxxr_core_tests PRIVATE ${LINUXXR_TEST_WARNINGS})
target_link_libraries(linuxxr_core_tests PRIVATE linuxxr_core)
add_test(NAME linuxxr_core_tests COMMAND linuxxr_core_tests)
set_tests_properties(linuxxr_core_tests PROPERTIES TIMEOUT 60)

# --- Fake Unity host ---------------------------------------------------------------------------
# Loads libLinuxXR.so via dlopen (no link against the plugin) and simulates the Unity Editor.
# Only built if the plugin target exists (Unity XR headers present).
if(TARGET LinuxXR)
  add_executable(linuxxr_fake_unity_host fake_unity_host.cpp)
  target_compile_options(linuxxr_fake_unity_host PRIVATE ${LINUXXR_TEST_WARNINGS})
  # Include the Unity headers as SYSTEM: warnings from third-party headers are not our concern.
  target_include_directories(linuxxr_fake_unity_host SYSTEM PRIVATE ${LINUXXR_UNITY_HEADERS} ${VULKAN_INCLUDE_DIR})
  target_compile_definitions(linuxxr_fake_unity_host PRIVATE VK_NO_PROTOTYPES
                             LINUXXR_EXPECTED_VERSION="${PROJECT_VERSION}")
  target_link_libraries(linuxxr_fake_unity_host PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(linuxxr_fake_unity_host LinuxXR)

  # Hermetic environment: no real OpenXR runtime; cache and sockets live in the build folder.
  set(LINUXXR_TEST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime")
  set(LINUXXR_TEST_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/xdg-cache")
  file(MAKE_DIRECTORY "${LINUXXR_TEST_RUNTIME_DIR}" "${LINUXXR_TEST_CACHE_DIR}")
  # XDG_RUNTIME_DIR must be 0700 for some loaders
  file(CHMOD "${LINUXXR_TEST_RUNTIME_DIR}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

  # Dummy manifest so that no runtime manifest installed on the system (e.g. /etc/xdg/openxr/1/
  # active_runtime.json -> Monado) gets picked: LINUXXR_RUNTIME=/nonexistent/none.json yields no
  # candidate, and the fallback list would then contain real system runtimes (kind "other" always counts
  # as reachable -> the plugin would call xrCreateInstance against a real runtime). XR_RUNTIME_JSON is
  # honored by the plugin and ranks before the fallbacks; "wivrn" in the name -> kind WiVRn -> without a
  # socket in $XDG_RUNTIME_DIR "not reachable" -> PreInit uses the cache/base list without loading OpenXR.
  set(LINUXXR_TEST_FAKE_RUNTIME "${CMAKE_CURRENT_BINARY_DIR}/fake-runtime/openxr_wivrn.json")
  file(WRITE "${LINUXXR_TEST_FAKE_RUNTIME}"
       "{\n  \"file_format_version\": \"1.0.0\",\n  \"runtime\": { \"library_path\": \"/nonexistent/libopenxr_fake.so\" }\n}\n")

  add_test(NAME linuxxr_fake_unity_host
           COMMAND linuxxr_fake_unity_host $<TARGET_FILE:LinuxXR>
           WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(linuxxr_fake_unity_host PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "XDG_RUNTIME_DIR=${LINUXXR_TEST_RUNTIME_DIR};XDG_CACHE_HOME=${LINUXXR_TEST_CACHE_DIR};LINUXXR_RUNTIME=/nonexistent/none.json;XR_RUNTIME_JSON=${LINUXXR_TEST_FAKE_RUNTIME}")

  # --- Integration test with a real (headless) Monado -----------------------------------------------
  # Starts monado-service itself; without Monado/Vulkan/OpenXR loader it ends with "SKIP" (exit 77 -> ctest "Skipped").
  # Own environment (runtime dir, cache, cwd are created in the test via mkdtemp) - deliberately NOT the hermetic
  # "no runtime" environment of the fake host above.
  add_executable(linuxxr_monado_integration monado_integration.cpp)
  target_compile_options(linuxxr_monado_integration PRIVATE ${LINUXXR_TEST_WARNINGS})
  target_include_directories(linuxxr_monado_integration SYSTEM PRIVATE ${LINUXXR_UNITY_HEADERS} ${VULKAN_INCLUDE_DIR})
  target_compile_definitions(linuxxr_monado_integration PRIVATE VK_NO_PROTOTYPES
                             LINUXXR_EXPECTED_VERSION="${PROJECT_VERSION}")
  target_link_libraries(linuxxr_monado_integration PRIVATE ${CMAKE_DL_LIBS} pthread)
  add_dependencies(linuxxr_monado_integration LinuxXR)
  add_test(NAME linuxxr_monado_integration
           COMMAND linuxxr_monado_integration $<TARGET_FILE:LinuxXR> 12
           WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(linuxxr_monado_integration PROPERTIES
    TIMEOUT 180
    SKIP_RETURN_CODE 77
    LABELS "integration")

  # Instance loss: Monado is killed mid-operation - the plugin must not hang (watchdog in the test).
  add_test(NAME linuxxr_monado_instance_loss
           COMMAND linuxxr_monado_integration $<TARGET_FILE:LinuxXR> 3
           WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(linuxxr_monado_instance_loss PROPERTIES
    TIMEOUT 180
    SKIP_RETURN_CODE 77
    LABELS "integration")
else()
  message(STATUS "Tests: target LinuxXR missing - linuxxr_fake_unity_host will not be built")
endif()
