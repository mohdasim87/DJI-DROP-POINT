set(DROPPOINT_TEST_SOURCES
    main.cpp
    test_atmosphere.cpp
    test_wind.cpp
    test_ballistics.cpp
    test_geodesy.cpp
    test_projection_raycast.cpp
    test_terrain.cpp
    test_gating.cpp
    test_dispersion.cpp
    test_timestamp_alignment.cpp
)

if(DROPPOINT_BUILD_PSDK)
    list(APPEND DROPPOINT_TEST_SOURCES test_psdk_credentials.cpp)
endif()

add_executable(droppoint_core_tests ${DROPPOINT_TEST_SOURCES})
target_link_libraries(droppoint_core_tests PRIVATE droppoint_core droppoint_util)
target_include_directories(droppoint_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(DROPPOINT_BUILD_PSDK)
    target_link_libraries(droppoint_core_tests PRIVATE droppoint_psdk)
endif()

target_compile_definitions(droppoint_core_tests PRIVATE
    DROPPOINT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_test(NAME droppoint_core_tests COMMAND droppoint_core_tests)
