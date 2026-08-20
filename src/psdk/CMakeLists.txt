# psdk/ — real DJI Payload-SDK hardware adapters.
#
# Only built when DROPPOINT_BUILD_PSDK=ON. The credential loader and release
# GPIO are buildable without vendored PSDK headers; the full DjiCore_Init and
# telemetry/gimbal/rangefinder/display/audio/liveview adapters require the
# vendored Payload-SDK under third_party/psdk/ (see docs/DEPLOYMENT.md).

# Allow the user to point CMake at a vendored PSDK tree. Defaults to
# ../psdk-upstream so that the clone created during research can be used
# directly during development on Windows.
set(DROPPOINT_PSDK_ROOT "${CMAKE_SOURCE_DIR}/../psdk-upstream" CACHE PATH
    "Path to the DJI Payload-SDK checkout containing psdk_lib/include and psdk_lib/lib")

set(DROPPOINT_PSDK_INCLUDE "${DROPPOINT_PSDK_ROOT}/psdk_lib/include")
set(DROPPOINT_PSDK_LIB_DIR "${DROPPOINT_PSDK_ROOT}/psdk_lib/lib")

add_library(droppoint_psdk STATIC
    app_registration.cpp
    app_registration.h
    psdk_util.cpp
    psdk_util.h
    release_gpio.cpp
    release_gpio.h
    platform_linux.cpp
    platform_linux.h
    telemetry_subscription.cpp
    telemetry_subscription.h
    gimbal_psdk.cpp
    gimbal_psdk.h
    rangefinder_camera_manager.cpp
    rangefinder_camera_manager.h
    display_floating_window.cpp
    display_floating_window.h
    audio_speaker.cpp
    audio_speaker.h
    liveview_marker.cpp
    liveview_marker.h
)

target_include_directories(droppoint_psdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${DROPPOINT_PSDK_INCLUDE}
)

target_compile_features(droppoint_psdk PUBLIC cxx_std_17)
target_link_libraries(droppoint_psdk PUBLIC
    droppoint_core
    droppoint_hal
    droppoint_util
    yaml-cpp
)

# On Linux we can link the real libpayloadsdk.a. On Windows/MSVC the static
# archive is not compatible, so we only add it when building for Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(DROPPOINT_PSDK_LIB "${DROPPOINT_PSDK_LIB_DIR}/aarch64-linux-gnu-gcc/libpayloadsdk.a")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
        set(DROPPOINT_PSDK_LIB "${DROPPOINT_PSDK_LIB_DIR}/x86_64-linux-gnu-gcc/libpayloadsdk.a")
    else()
        message(FATAL_ERROR "Unsupported Linux processor for PSDK: ${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(NOT EXISTS ${DROPPOINT_PSDK_LIB})
        message(FATAL_ERROR "PSDK static library not found at ${DROPPOINT_PSDK_LIB}. "
            "Set DROPPOINT_PSDK_ROOT to a checkout that has been built/installed for the target.")
    endif()

    target_link_libraries(droppoint_psdk PUBLIC ${DROPPOINT_PSDK_LIB})
endif()

# Real PSDK application binary. Only created on Linux because the executable
# needs the PSDK static library linked.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(droppoint main_psdk.cpp)
    target_link_libraries(droppoint PRIVATE droppoint_app droppoint_psdk)
endif()
