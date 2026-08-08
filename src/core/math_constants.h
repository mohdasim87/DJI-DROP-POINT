// core/math_constants.h
//
// M_PI is not guaranteed by the C++ standard (MSVC hides it behind
// _USE_MATH_DEFINES). DropPoint targets both a Linux aarch64 companion
// computer and Windows/Linux development hosts, so core/ defines its own
// constant rather than depending on a platform macro.
#pragma once

namespace droppoint::core {
inline constexpr double kPi = 3.14159265358979323846;
}  // namespace droppoint::core
