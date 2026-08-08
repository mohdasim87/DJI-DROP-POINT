// hal/i_rangefinder.h
//
// Laser rangefinder access, via DjiCameraManager_GetLaserRangingInfo on real
// hardware (docs/PSDK-RESEARCH.md section 5), documented update rate <= 5
// Hz. Used for Source B's direct target latch and for the Step 7 DEM datum
// bias correction (core/terrain.h).
#pragma once

#include <cstdint>
#include <optional>

#include "core/solution.h"

namespace droppoint::hal {

struct RangefinderReading {
    bool valid = false;
    core::LatLonAlt geodetic;      // laser-derived geographic coordinate
    double slant_range_m = 0.0;
    std::uint64_t timestamp_us = 0;
};

class IRangefinder {
public:
    virtual ~IRangefinder() = default;

    // Most recent reading; nullopt if the rangefinder has never fired or
    // isn't fitted (see aircraft.yaml `rangefinder_present`).
    virtual std::optional<RangefinderReading> GetLatestReading() = 0;

    // Requests a fresh measurement along the current optical axis (Source B
    // "fire the laser rangefinder and latch the result"). Result arrives via
    // GetLatestReading() on the next poll; real hardware is rate-limited to
    // ~5 Hz per docs/PSDK-RESEARCH.md.
    virtual void RequestMeasurement() = 0;
};

}  // namespace droppoint::hal
