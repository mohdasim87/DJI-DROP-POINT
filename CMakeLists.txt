cmake_minimum_required(VERSION 3.16)
# Single version source: PROJECT_VERSION_* feeds util/version.h,
# DjiCore_SetFirmwareVersion and the Manifold 3 app.json firmware_version.
project(droppoint VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DROPPOINT_BUILD_TESTS "Build unit tests" ON)
option(DROPPOINT_BUILD_PSDK "Build the PSDK (DJI hardware) target. Requires the Payload-SDK vendored under third_party/psdk." OFF)
option(DROPPOINT_BUILD_TOOLS "Build offline tools (calibrate, replay, lenscal)" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Version/build hash (spec 15.3: must appear in the startup log, self-test
# report, and floating window on request). Falls back to the project version
# outside a git checkout (e.g. a source tarball, or a container where git
# refuses the checkout's ownership) rather than failing the build.
find_package(Git QUIET)
set(DROPPOINT_BUILD_VERSION "${PROJECT_VERSION}")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE DROPPOINT_BUILD_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(DROPPOINT_BUILD_VERSION STREQUAL "")
    set(DROPPOINT_BUILD_VERSION "${PROJECT_VERSION}")
  endif()
endif()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/util/version.h
  @ONLY
)

# Vendor license public key: packaging/license_public_key.txt (64 hex
# chars) is embedded into the binary. Missing file -> all-zero key ->
# VerifyLicense fails closed (no license is ever accepted).
set(DROPPOINT_LICENSE_PUBKEY "0000000000000000000000000000000000000000000000000000000000000000")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/license_public_key.txt")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packaging/license_public_key.txt" DROPPOINT_LICENSE_PUBKEY)
  string(STRIP "${DROPPOINT_LICENSE_PUBKEY}" DROPPOINT_LICENSE_PUBKEY)
  # Re-run configure when the key changes so the embedded copy refreshes.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${CMAKE_CURRENT_SOURCE_DIR}/packaging/license_public_key.txt")
else()
  message(WARNING "packaging/license_public_key.txt missing — embedding an "
          "all-zero license key; no license will verify (fail closed)")
endif()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util/license_pubkey.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/util/license_pubkey.h
  @ONLY
)

# Monocypher: vendored single-file Ed25519 (third_party/monocypher, CC0),
# used by util/license.cpp and the droppoint-license tool.
add_library(monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(monocypher PUBLIC third_party/monocypher)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# yaml-cpp: the only external dependency in the whole tree, used solely by
# util/config.* to load and schema-validate the YAML files in config/.
# Pinned to a tagged release well past its publish date (not "latest").
# ---------------------------------------------------------------------------
include(FetchContent)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4), which CMake
# 4.x rejects. Temporarily lower the policy floor around the FetchContent
# call so the pinned tag configures unmodified.
set(DROPPOINT_SAVED_POLICY_MIN ${CMAKE_POLICY_VERSION_MINIMUM})
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)
# yaml-cpp 0.8.0 uses uint16_t/uint32_t without including <cstdint>, which
# GCC 13+ (Ubuntu 24.04 CI image) rejects. Force-include it on non-MSVC so
# the pinned tag builds unmodified.
if(NOT MSVC AND TARGET yaml-cpp)
  target_compile_options(yaml-cpp PRIVATE -include cstdint)
endif()
if(DROPPOINT_SAVED_POLICY_MIN)
  set(CMAKE_POLICY_VERSION_MINIMUM ${DROPPOINT_SAVED_POLICY_MIN})
else()
  unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

# ---------------------------------------------------------------------------
# core: pure C++17, zero DJI/platform dependencies, fully unit-testable.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/util)
add_subdirectory(src/hal)
add_subdirectory(src/sim)
add_subdirectory(src/app)
add_subdirectory(src/hw)

if(DROPPOINT_BUILD_PSDK)
  add_subdirectory(src/psdk)
endif()

if(DROPPOINT_BUILD_TOOLS)
  add_subdirectory(tools/calibrate)
  add_subdirectory(tools/replay)
  add_subdirectory(tools/lenscal)
  add_subdirectory(tools/license)
endif()

if(DROPPOINT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
