// psdk/psdk_util.cpp
//
// Small implementation details that need a .cpp translation unit.
#include "psdk_util.h"

#ifdef __linux__
#include <time.h>
#else
#include <chrono>
#endif

namespace droppoint::psdk {

std::uint64_t MonotonicUs() {
#ifdef __linux__
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000ULL;
#else
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
#endif
}

}  // namespace droppoint::psdk
