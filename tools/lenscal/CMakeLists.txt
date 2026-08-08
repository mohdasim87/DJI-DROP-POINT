# tools/lenscal — see main.cpp header comment: the full checkerboard
# auto-calibration path requires OpenCV, which is not vendored in this
# repository (see docs/OPEN-QUESTIONS.md). This binary implements the
# manual-entry path only and documents the OpenCV path as a follow-up.
add_executable(droppoint_lenscal main.cpp)
target_link_libraries(droppoint_lenscal PRIVATE droppoint_core droppoint_util)
