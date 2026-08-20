# psdk/ — real DJI Payload-SDK hardware adapters.
#
# Only built when DROPPOINT_BUILD_PSDK=ON. The credential loader and release
# GPIO are buildable without vendored PSDK headers; the full DjiCore_Init and
# telemetry/gimbal/rangefinder/display/audio/liveview adapters require the
# vendored Payload-SDK under third_party/psdk/ (see docs/DEPLOYMENT.md).

add_library(droppoint_psdk STATIC
    app_registration.cpp
    release_gpio.cpp
)

target_include_directories(droppoint_psdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(droppoint_psdk PUBLIC cxx_std_17)
target_link_libraries(droppoint_psdk PUBLIC
    droppoint_core
    droppoint_hal
    droppoint_util
    yaml-cpp
)

# Real PSDK application binary. The entry point would live here once the
# DjiCore_Init wiring and platform_linux.cpp are implemented.
# add_executable(droppoint main_psdk.cpp)
# target_link_libraries(droppoint PRIVATE droppoint_app droppoint_psdk)
