cmake_minimum_required(VERSION 3.16)
project(droppoint LANGUAGES CXX)

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
# report, and floating window on request). Falls back to "unknown" outside
# a git checkout (e.g. a source tarball) rather than failing the build.
find_package(Git QUIET)
set(DROPPOINT_BUILD_VERSION "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE DROPPOINT_BUILD_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(DROPPOINT_BUILD_VERSION STREQUAL "")
    set(DROPPOINT_BUILD_VERSION "unknown")
  endif()
endif()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/util/version.h
  @ONLY
)

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

if(DROPPOINT_BUILD_PSDK)
  add_subdirectory(src/psdk)
endif()

if(DROPPOINT_BUILD_TOOLS)
  add_subdirectory(tools/calibrate)
  add_subdirectory(tools/replay)
  add_subdirectory(tools/lenscal)
endif()

if(DROPPOINT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
