# sim/ — simulator implementation of every HAL interface (spec section 12).
# Depends on core/ and hal/ only; no DJI headers, ever.
add_library(droppoint_sim STATIC
    scenario.cpp
    sim_world.cpp
    sim_hal.cpp
    scripted_pilot.cpp
)
target_include_directories(droppoint_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(droppoint_sim PUBLIC cxx_std_17)
target_link_libraries(droppoint_sim PUBLIC droppoint_core droppoint_hal droppoint_app)
