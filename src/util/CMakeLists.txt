# util/ — small, dependency-free helpers shared by hal/, sim/, app/, and
# tools/. Like core/, this does not depend on DJI headers, but unlike core/
# it is allowed to do OS-level things (clocks, logging sinks) since it is
# not part of the pure-math boundary.
add_library(droppoint_util STATIC
    ring_buffer.h
    timesync.cpp
    logging.cpp
    config.cpp
    drop_kits.cpp
    drop_log.cpp
    sha256.cpp
    license.cpp
)
target_include_directories(droppoint_util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_features(droppoint_util PUBLIC cxx_std_17)
target_link_libraries(droppoint_util PUBLIC droppoint_core yaml-cpp::yaml-cpp monocypher)
