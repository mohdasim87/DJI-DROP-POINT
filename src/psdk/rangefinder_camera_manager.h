// psdk/rangefinder_camera_manager.h
//
// Status: unverified — requires hardware.
//
// hal::IRangefinder backed by DjiCameraManager_GetLaserRangingInfo —
// Manifold-3-only (camera management is an advanced feature, unavailable on
// the generic-companion M4T/M400 link). Instantiated by main only when
// aircraft.yaml says advanced_features && rangefinder_present; everywhere
// else the NullRangefinder below is used instead.
//
// The laser is POLLED (no async fire-and-latch API on this link):
// RequestMeasurement() marks a request and the next GetLatestReading()
// performs the blocking SDK call, rate-limited to 2 Hz.
#pragma once

#include <cstdint>

#include "hal/i_rangefinder.h"

namespace droppoint::psdk {

class NullRangefinder : public hal::IRangefinder {
public:
    std::optional<hal::RangefinderReading> GetLatestReading() override { return std::nullopt; }
    void RequestMeasurement() override {}
};

class PsdkRangefinder : public hal::IRangefinder {
public:
    // mount_position is aircraft.yaml camera_mount_position (an
    // E_DjiMountPosition value).
    explicit PsdkRangefinder(int mount_position);

    // Calls DjiCameraManager_Init(). Throws std::runtime_error on failure —
    // main catches it and falls back to NullRangefinder with a logged
    // warning (a rangefinder is an aid, never a flight-critical input).
    void Init();

    std::optional<hal::RangefinderReading> GetLatestReading() override;
    void RequestMeasurement() override;

    // True once Init() succeeded — surfaced in the self-test report.
    bool IsInitialised() const { return initialised_; }

private:
    int mount_position_;
    bool initialised_ = false;
    bool measurement_requested_ = false;
    std::uint64_t last_poll_us_ = 0;
    hal::RangefinderReading latest_;
    bool have_latest_ = false;
};

}  // namespace droppoint::psdk
