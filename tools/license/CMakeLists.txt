# tools/license — vendor-side key tool (Windows + Linux dev machines).
# NOT shipped in the .deb / .dpk / customer zip: it is the only component
# that ever touches the private signing key.
add_executable(droppoint-license main.cpp)
target_link_libraries(droppoint-license PRIVATE droppoint_util)
if(WIN32)
    target_link_libraries(droppoint-license PRIVATE bcrypt)
endif()
