set(DROPPOINT_TEST_SOURCES
    main.cpp
    test_atmosphere.cpp
    test_wind.cpp
    test_wind_estimator.cpp
    test_ballistics.cpp
    test_geodesy.cpp
    test_projection_raycast.cpp
    test_terrain.cpp
    test_gating.cpp
    test_dispersion.cpp
    test_timestamp_alignment.cpp
    test_mission_state.cpp
    test_release_manager.cpp
    test_guidance_loop_sim.cpp
    test_config_profiles.cpp
    test_hw_conversions.cpp
    test_operator_inputs.cpp
    test_widget_ui.cpp
    test_terrain_provider.cpp
    test_pwm_servo_release.cpp
    test_sd_notify.cpp
    test_selftest_report.cpp
    test_file_ownership.cpp
    test_drop_kits.cpp
    test_auth_chip.cpp
    test_license.cpp
)

if(DROPPOINT_BUILD_PSDK)
    list(APPEND DROPPOINT_TEST_SOURCES test_psdk_credentials.cpp)
endif()

add_executable(droppoint_core_tests ${DROPPOINT_TEST_SOURCES})
target_link_libraries(droppoint_core_tests PRIVATE droppoint_core droppoint_util droppoint_app droppoint_sim droppoint_hw yaml-cpp monocypher)
target_include_directories(droppoint_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(DROPPOINT_BUILD_PSDK)
    target_link_libraries(droppoint_core_tests PRIVATE droppoint_psdk)
endif()

target_compile_definitions(droppoint_core_tests PRIVATE
    DROPPOINT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DROPPOINT_CONFIG_DIR="${CMAKE_SOURCE_DIR}/config"
    DROPPOINT_WIDGET_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/widget"
    DROPPOINT_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)

add_test(NAME droppoint_core_tests COMMAND droppoint_core_tests)
