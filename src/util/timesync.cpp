#include "timesync.h"

#include <numeric>

namespace droppoint::util {

TimeSync::TimeSync(std::size_t offset_window_size) : window_size_(offset_window_size) {}

void TimeSync::OnPpsEdge(std::uint64_t local_monotonic_us) {
    last_pps_local_us_ = local_monotonic_us;
    mode_ = TimeSyncMode::kHardwarePps;
}

void TimeSync::OnTelemetrySample(std::uint64_t local_monotonic_us, std::uint64_t aircraft_time_us) {
    // Even while PPS-disciplined, keep the software estimate warm so a lost
    // PPS signal degrades gracefully rather than jumping.
    const double sample_offset =
        static_cast<double>(aircraft_time_us) - static_cast<double>(local_monotonic_us);
    offset_samples_.push_back(sample_offset);
    if (offset_samples_.size() > window_size_) offset_samples_.pop_front();

    offset_us_ = std::accumulate(offset_samples_.begin(), offset_samples_.end(), 0.0) /
                 static_cast<double>(offset_samples_.size());

    if (mode_ == TimeSyncMode::kNone) mode_ = TimeSyncMode::kSoftwareOffset;
}

std::uint64_t TimeSync::ToAircraftTime(std::uint64_t local_monotonic_us) const {
    return static_cast<std::uint64_t>(static_cast<double>(local_monotonic_us) + offset_us_);
}

}  // namespace droppoint::util
