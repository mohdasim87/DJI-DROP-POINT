// sim/sim_world.h
//
// The simulated aircraft + environment. Advances a simple kinematic model
// (velocity-commanded with a bounded acceleration), a power-law wind model
// (mean + band-limited gust noise), emulates the hover tilt a real
// multirotor shows holding position in wind, and injects realistic
// timestamps/latency/jitter so the simulator exercises the same timing
// bugs real hardware would (spec 12).
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
    // guidance-loop tick from the sim driver.
    void Step(double dt_s);

    // Commands the aircraft's horizontal ENU velocity. The world slews the
    // actual velocity toward the command at a bounded acceleration
    // (0.4 m/s^2), like a real multirotor's velocity controller.
    void SetCommandedVelocity(const core::EnuVector& velocity_mps);

    std::uint64_t NowUs() const { return now_us_; }
    double NowS() const { return t_s_; }

    // Telemetry as it would arrive from the aircraft: may be stale/dropped
    // out during a kTelemetryDropout window, and carries jitter. Attitude
    // includes the emulated lean-into-wind hover tilt.
    std::optional<core::AircraftState> LatestTelemetry() const;
    core::GimbalState GimbalAttitude() const;
    double GroundElevationAt(double lat_deg, double lon_deg) const;
    core::LatLonAlt CurrentGeodetic() const;

    // Truth wind at a given height above target ground, per the scenario's
    // mean + gust model through the power-law profile.
    core::EnuVector WindAtHeight(double height_agl_m) const;

    // Truth payload fall: integrates the released payload with the TRUTH
    // wind from the current (release-instant) aircraft state. Called by the
    // sim driver when the release fires; the result is what the predicted
    // impact is scored against.
    void SimulateTruthDrop(const core::PayloadProfile& payload, const core::EnuPoint& aim_enu);
    bool HasTruthImpact() const { return truth_impact_valid_; }
    core::EnuPoint TruthImpactEnu() const { return truth_impact_; }

    // GNSS/RTK simulated state, degraded by kGnssDegradation / kRtkLoss.
    int SimulatedSatelliteCount() const;
    bool SimulatedRtkFix() const;

    bool GimbalSlewing() const { return config_.type == ScenarioType::kGimbalSlew; }

    const ScenarioConfig& Config() const { return config_; }
    core::EnuPoint Position() const { return position_; }
    core::EnuVector Velocity() const { return velocity_; }

private:
    double WindSpeedAtHeight(double height_agl_m) const;

    ScenarioConfig config_;
    std::uint64_t now_us_ = 0;
    double t_s_ = 0.0;
    core::EnuPoint position_{0, 0, 60.0};
    core::EnuVector velocity_{0, 0, 0};
    core::EnuVector commanded_velocity_{0, 0, 0};
    double heading_rad_ = 0.0;  // compass heading, rad
    mutable std::mt19937_64 rng_;
    mutable std::normal_distribution<double> jitter_dist_;

    bool truth_impact_valid_ = false;
    core::EnuPoint truth_impact_{};
};

}  // namespace droppoint::sim
