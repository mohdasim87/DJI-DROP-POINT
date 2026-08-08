# hal/ — pure interfaces (abstract base classes). Header-only, so this is an
# INTERFACE target: nothing to compile, just include paths and a dependency
# on core/ for the shared structs.
add_library(droppoint_hal INTERFACE)
target_include_directories(droppoint_hal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(droppoint_hal INTERFACE droppoint_core)
