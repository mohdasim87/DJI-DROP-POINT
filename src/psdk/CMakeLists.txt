# psdk/ — real DJI Payload-SDK hardware adapters + the `droppoint` binary.
#
# Only configured when DROPPOINT_BUILD_PSDK=ON. The credential loader is
# DJI-free and always builds (its test runs on every host); the adapter
# sources, the DJI sample platform C files, the libpayloadsdk.a link and the
# executable are LINUX ONLY — skipped with a message elsewhere.

set(DROPPOINT_PSDK_ROOT "${CMAKE_SOURCE_DIR}/third_party/psdk" CACHE PATH
    "Path to the DJI Payload-SDK checkout containing psdk_lib/include and psdk_lib/lib")
if(NOT EXISTS "${DROPPOINT_PSDK_ROOT}/psdk_lib/include/dji_core.h")
    if(EXISTS "${CMAKE_SOURCE_DIR}/../psdk-upstream/psdk_lib/include/dji_core.h")
        set(DROPPOINT_PSDK_ROOT "${CMAKE_SOURCE_DIR}/../psdk-upstream" CACHE PATH
            "Path to the DJI Payload-SDK checkout" FORCE)
    endif()
endif()

set(DROPPOINT_PSDK_INCLUDE "${DROPPOINT_PSDK_ROOT}/psdk_lib/include")
set(DROPPOINT_PSDK_LIB_DIR "${DROPPOINT_PSDK_ROOT}/psdk_lib/lib")
set(DROPPOINT_PSDK_OSAL "${DROPPOINT_PSDK_ROOT}/samples/sample_c/platform/linux/common/osal")
set(DROPPOINT_PSDK_HAL "${DROPPOINT_PSDK_ROOT}/samples/sample_c/platform/linux/manifold3/hal")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS
        "droppoint: PSDK adapter sources and the 'droppoint' executable are "
        "Linux-only — building just the credential loader on ${CMAKE_SYSTEM_NAME}")
    add_library(droppoint_psdk STATIC app_registration.cpp app_registration.h)
    target_include_directories(droppoint_psdk PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_compile_features(droppoint_psdk PUBLIC cxx_std_17)
    target_link_libraries(droppoint_psdk PUBLIC droppoint_util yaml-cpp)
    return()
endif()

add_library(droppoint_psdk STATIC
    app_registration.cpp
    app_registration.h
    hal_i2c.cpp
    hal_i2c.h
    hal_uart.cpp
    hal_uart.h
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
    audio_hms.cpp
    audio_hms.h
    widget_bridge.cpp
    widget_bridge.h
    release_widget.cpp
    release_widget.h
    probe_widgets.cpp
    probe_widgets.h
)

target_include_directories(droppoint_psdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${DROPPOINT_PSDK_INCLUDE}
    ${DROPPOINT_PSDK_OSAL}
    ${DROPPOINT_PSDK_HAL}
)

target_compile_features(droppoint_psdk PUBLIC cxx_std_17)
target_link_libraries(droppoint_psdk PUBLIC
    droppoint_app
    droppoint_hw
    droppoint_util
    yaml-cpp
)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(DROPPOINT_PSDK_LIB "${DROPPOINT_PSDK_LIB_DIR}/aarch64-linux-gnu-gcc/libpayloadsdk.a")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    set(DROPPOINT_PSDK_LIB "${DROPPOINT_PSDK_LIB_DIR}/x86_64-linux-gnu-gcc/libpayloadsdk.a")
else()
    message(FATAL_ERROR "Unsupported Linux processor for PSDK: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(NOT EXISTS ${DROPPOINT_PSDK_LIB})
    message(FATAL_ERROR "PSDK static library not found at ${DROPPOINT_PSDK_LIB}. "
        "Set DROPPOINT_PSDK_ROOT to a checkout with psdk_lib/lib/<arch>-linux-gnu-gcc.")
endif()

# DJI's own platform sources, compiled straight from the PSDK checkout (the
# build spec forbids copying them into this repo): the POSIX OSAL + socket +
# filesystem shims and the Manifold 3 functionfs USB-bulk HAL.
add_library(droppoint_psdk_platform STATIC
    ${DROPPOINT_PSDK_OSAL}/osal.c
    ${DROPPOINT_PSDK_OSAL}/osal_fs.c
    ${DROPPOINT_PSDK_OSAL}/osal_socket.c
    ${DROPPOINT_PSDK_HAL}/hal_usb_bulk.c
)
target_include_directories(droppoint_psdk_platform PUBLIC
    ${DROPPOINT_PSDK_INCLUDE}
    ${DROPPOINT_PSDK_OSAL}
    ${DROPPOINT_PSDK_HAL}
)
# The DJI sample C code needs POSIX/GNU extensions and compiles with its own
# warnings; keep it at the sample's dialect and quiet about it.
# _GNU_SOURCE: DJI's sample build sets it (pthread_setname_np in osal.c); without
# it glibc hides the prototype and GCC 14+/clang turn the implicit decl into a
# hard error.
target_compile_definitions(droppoint_psdk_platform PRIVATE _GNU_SOURCE)
target_compile_options(droppoint_psdk_platform PRIVATE
    -std=gnu99 -pthread -Wno-unused-parameter -Wno-unused-variable)

add_executable(droppoint main_psdk.cpp)
target_link_libraries(droppoint PRIVATE
    droppoint_psdk
    droppoint_psdk_platform
    ${DROPPOINT_PSDK_LIB}
    m dl pthread
)
# Static libstdc++/libgcc so the .deb's only runtime dependency is glibc.
target_link_options(droppoint PRIVATE -static-libstdc++ -static-libgcc)
