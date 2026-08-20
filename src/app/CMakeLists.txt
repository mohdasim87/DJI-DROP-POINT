# app/ — application logic: state machine, guidance loop, release manager,
# and the executable entry point. Links core/, hal/, util/, and sim/ (the
# PSDK link only exists in the separate `droppoint` PSDK target, gated by
# DROPPOINT_BUILD_PSDK, see src/psdk/CMakeLists.txt).
add_library(droppoint_app STATIC
    mission_state.cpp
    release_manager.cpp
    guidance_loop.cpp
    crosshair_target_source.cpp
)
target_include_directories(droppoint_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(droppoint_app PUBLIC cxx_std_17)
target_link_libraries(droppoint_app PUBLIC droppoint_core droppoint_hal droppoint_util)

add_executable(droppoint_sim_main main.cpp)
target_link_libraries(droppoint_sim_main PRIVATE droppoint_app droppoint_sim droppoint_util)
