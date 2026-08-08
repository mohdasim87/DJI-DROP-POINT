// sim/sim_world.h
//
// The simulated aircraft + environment. Advances a simple kinematic model
// (constant-velocity run-in or hover) plus a wind model (mean + band-limited
// gust noise), and injects realistic timestamps/latency/jitter so the
// simulator exercises the same timing bugs real hardware would (spec 12).
#pragma once

#include <cstdint>
#include <optional>
#include <random>

#include "core/solution.h"
#include "scenario.h"

namespace droppoint::sim {

class SimWorld {
public:
    explicit SimWorld(ScenarioConfig config, std::uint64_t seed = 12345);

    // Advances the world by dt_s of wall-clock time. Call this once per
    // guidance-loop tick from main's sim driver.
    void Step(double dt_s);

    std::uint64_t NowUs() const { return now_us_; }

    // Telemetry as it would arrive from the aircraft: may be stale/dropped
    // out during a kTelemetryDropout window, and carries jitter.
    std::optional<core::AircraftState> LatestTelemetry() const;
    core::GimbalState GimbalAttitude() const;
    double GroundElevationAt(double lat_deg, double lon_deg) const;
    core::LatLonAlt CurrentGeodetic() const;

    // Wind at a given height AGL, per the scenario's mean + gust model.
    core::EnuVector WindAtHeight(double height_agl_m) const;

    // GNSS/RTK simulated state, degraded by kGnssDegradation / kRtkLoss.
    int SimulatedSatelliteCount() const;
    bool SimulatedRtkFix() const;

    bool GimbalSlewing() const { return config_.type == ScenarioType::kGimbalSlew; }

    const ScenarioConfig& Config() const { return config_; }

private:
    ScenarioConfig config_;
    std::uint64_t now_us_ = 0;
    double t_s_ = 0.0;
    core::EnuPoint position_{0, 0, 60.0};
    core::EnuVector velocity_{0, 0, 0};
    double heading_rad_ = 0.0;
    mutable std::mt19937_64 rng_;
    mutable std::normal_distribution<double> jitter_dist_;
};

}  // namespace droppoint::sim
