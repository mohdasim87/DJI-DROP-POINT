# hw/ — always-built, DJI-header-free hardware-facing logic: PSDK protocol
# conversions, the Pilot 2 widget definition + config generator + input
# latch, the SRTM terrain provider, the sysfs-PWM release backend and the
# systemd sd_notify helper. Everything here is unit-testable on any OS.
add_library(droppoint_hw STATIC
    conversions.cpp
    operator_inputs.cpp
    widget_ui.cpp
    terrain_provider.cpp
    pwm_servo_release.cpp
    sd_notify.cpp
    selftest_report.cpp
    file_ownership.cpp
    auth_chip.cpp
)
target_include_directories(droppoint_hw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(droppoint_hw PUBLIC cxx_std_17)
target_link_libraries(droppoint_hw PUBLIC droppoint_app)
