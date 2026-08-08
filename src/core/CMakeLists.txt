# core/ — pure C++17, zero DJI or platform dependencies, fully unit-tested.
# Nothing in this target may link against a DJI header, an OS-specific
# header, or perform file/network I/O. Structs in, structs out.
add_library(droppoint_core STATIC
    atmosphere.cpp
    wind.cpp
    ballistics.cpp
    dispersion.cpp
    geodesy.cpp
    projection.cpp
    raycast.cpp
    terrain.cpp
    gating.cpp
)

# Two include roots: core/*.h files include each other by bare name
# ("solution.h"), while consumers outside core/ (hal/, sim/, app/) include
# via "core/solution.h" for clarity about the boundary they're crossing.
target_include_directories(droppoint_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_features(droppoint_core PUBLIC cxx_std_17)
