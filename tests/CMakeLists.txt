add_executable(droppoint_core_tests
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
target_link_libraries(droppoint_core_tests PRIVATE droppoint_core droppoint_util)
target_include_directories(droppoint_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME droppoint_core_tests COMMAND droppoint_core_tests)
