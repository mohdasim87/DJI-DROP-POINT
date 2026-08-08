// util/timesync.h
//
// Time synchronisation between the companion computer clock and the
// aircraft/video timebase (spec 5.5, 6.7). Prefers PPS hardware discipline
// (M300's PPS signal on pin A5) where available; falls back to a software
// offset estimator otherwise, and always reports which mode is active so
// the self-test (spec 15.3) can surface it.
#pragma once

#include <cstdint>
#include <deque>

namespace droppoint::util {

enum class TimeSyncMode { kNone, kSoftwareOffset, kHardwarePps };

class TimeSync {
public:
    explicit TimeSync(std::size_t offset_window_size = 200);

    // Called whenever a PPS edge is detected with the local monotonic
    // timestamp it occurred at; switches the sync mode to kHardwarePps.
    void OnPpsEdge(std::uint64_t local_monotonic_us);

    // Called with a (local_monotonic_us, reported_aircraft_time_us) pair
    // whenever a telemetry sample arrives, to maintain the software offset
    // estimate used when no PPS signal is present.
    void OnTelemetrySample(std::uint64_t local_monotonic_us, std::uint64_t aircraft_time_us);

    // Converts a local monotonic timestamp to the aircraft/video timebase.
    std::uint64_t ToAircraftTime(std::uint64_t local_monotonic_us) const;

    TimeSyncMode Mode() const { return mode_; }
    double CurrentOffsetUs() const { return offset_us_; }

private:
    TimeSyncMode mode_ = TimeSyncMode::kNone;
    double offset_us_ = 0.0;
    std::deque<double> offset_samples_;
    std::size_t window_size_;
    std::uint64_t last_pps_local_us_ = 0;
};

}  // namespace droppoint::util
