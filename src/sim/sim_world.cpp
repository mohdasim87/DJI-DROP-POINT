#include "sim_world.h"

#include <cmath>

#include "core/geodesy.h"
#include "core/math_constants.h"
#include "core/wind.h"

namespace droppoint::sim {

namespace {
constexpr core::LatLonAlt kOriginLla{47.6062, -122.3321, 30.0};
}

SimWorld::SimWorld(ScenarioConfig config, std::uint64_t seed)
    : config_(config), rng_(seed), jitter_dist_(0.0, config.telemetry_jitter_ms * 1000.0) {
    heading_rad_ = config_.run_in_heading_deg * core::kPi / 180.0;
    velocity_.e = config_.run_in_speed_mps * std::sin(heading_rad_);
    velocity_.n = config_.run_in_speed_mps * std::cos(heading_rad_);
}

void SimWorld::Step(double dt_s) {
    t_s_ += dt_s;
    now_us_ = static_cast<std::uint64_t>(t_s_ * 1e6);
    position_.e += velocity_.e * dt_s;
    position_.n += velocity_.n * dt_s;
}

bool SimWorld::SimulatedRtkFix() const {
    if (config_.type == ScenarioType::kRtkLoss) {
        // RTK drops out in a window mid-scenario to exercise the gate.
        return t_s_ < 5.0 || t_s_ > 10.0;
    }
    return true;
}

int SimWorld::SimulatedSatelliteCount() const {
    if (config_.type == ScenarioType::kGnssDegradation) {
        if (t_s_ > 5.0 && t_s_ < 10.0) return 6;  // below the default gate threshold of 12
    }
    return 18;
}

std::optional<core::AircraftState> SimWorld::LatestTelemetry() const {
    if (config_.type == ScenarioType::kTelemetryDropout && config_.dropout_start_s >= 0.0) {
        if (t_s_ >= config_.dropout_start_s && t_s_ < config_.dropout_start_s + config_.dropout_duration_s) {
            return std::nullopt;  // simulated dropout window
        }
    }

    core::AircraftState state;
    // Jitter is applied to the *reported* timestamp only, not the physical
    // state, to emulate transport-layer timing noise without corrupting the
    // actual kinematics under test.
    const double jitter_us = jitter_dist_(rng_);
    state.timestamp_us = static_cast<std::uint64_t>(std::max(0.0, t_s_ * 1e6 + jitter_us));
    state.position = position_;
    state.velocity = velocity_;
    state.yaw_rad = core::geodesy::WrapAngleRad(core::kPi / 2.0 - heading_rad_);
    state.pitch_rad = 0.0;
    state.roll_rad = 0.0;
    state.geodetic = CurrentGeodetic();
    state.valid = true;
    return state;
}

core::LatLonAlt SimWorld::CurrentGeodetic() const {
    const auto origin = core::geodesy::MakeEnuOrigin(kOriginLla);
    return core::geodesy::EnuToLla(position_, origin);
}

core::GimbalState SimWorld::GimbalAttitude() const {
    core::GimbalState gimbal;
    gimbal.timestamp_us = now_us_;
    gimbal.pitch_rad = -core::kPi / 3.0;  // nominal 60 deg depression
    gimbal.yaw_rad = 0.0;
    if (config_.type == ScenarioType::kGimbalSlew) {
        // Sinusoidal yaw slew to exercise timestamp-alignment code paths.
        gimbal.yaw_rad = 0.3 * std::sin(t_s_ * 1.0);
    }
    gimbal.roll_rad = 0.0;
    gimbal.zoom_ratio = 1.0;
    gimbal.valid = true;
    return gimbal;
}

double SimWorld::GroundElevationAt(double lat_deg, double lon_deg) const {
    if (config_.type != ScenarioType::kSlopingTerrain) return kOriginLla.alt_m_hae;
    // A simple linear slope rising to the north from the origin.
    const auto origin = core::geodesy::MakeEnuOrigin(kOriginLla);
    const core::EnuPoint p = core::geodesy::LlaToEnu(core::LatLonAlt{lat_deg, lon_deg, 0.0}, origin);
    const double rise = p.n * std::tan(config_.terrain_slope_deg * core::kPi / 180.0);
    return kOriginLla.alt_m_hae + rise;
}

core::EnuVector SimWorld::WindAtHeight(double height_agl_m) const {
    double speed = config_.wind_mean_mps;
    if (config_.wind_gust_amplitude_mps > 0.0) {
        speed += config_.wind_gust_amplitude_mps * std::sin(2.0 * core::kPi * config_.wind_gust_band_hz * t_s_);
    }
    if (config_.type == ScenarioType::kWindShear) {
        // Wind direction backs with height to exercise shear handling.
        const double from_deg = config_.wind_from_deg + height_agl_m * 0.3;
        return core::wind::ToVector(std::max(0.0, speed), from_deg * core::kPi / 180.0);
    }
    return core::wind::ToVector(std::max(0.0, speed), config_.wind_from_deg * core::kPi / 180.0);
}

}  // namespace droppoint::sim
