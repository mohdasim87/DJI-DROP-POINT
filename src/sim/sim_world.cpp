#include "sim_world.h"

#include <cmath>

#include "core/atmosphere.h"
#include "core/ballistics.h"
#include "core/geodesy.h"
#include "core/math_constants.h"
#include "core/wind.h"

namespace droppoint::sim {

namespace {
constexpr core::LatLonAlt kOriginLla{47.6062, -122.3321, 30.0};
constexpr double kMaxAccelerationMps2 = 0.4;  // velocity-command slew limit
constexpr double kWindAlpha = 0.14;           // truth power-law exponent (open country)
}

SimWorld::SimWorld(ScenarioConfig config, std::uint64_t seed)
    : config_(config), rng_(seed), jitter_dist_(0.0, config.telemetry_jitter_ms * 1000.0) {
    heading_rad_ = config_.run_in_heading_deg * core::kPi / 180.0;
    position_.u = config_.start_altitude_m;
}

void SimWorld::SetCommandedVelocity(const core::EnuVector& velocity_mps) {
    commanded_velocity_ = velocity_mps;
    commanded_velocity_.u = 0.0;  // altitude hold is not modelled
}

void SimWorld::Step(double dt_s) {
    t_s_ += dt_s;
    now_us_ = static_cast<std::uint64_t>(t_s_ * 1e6);

    // Slew the actual velocity toward the command at the acceleration
    // limit, then integrate position.
    const double de = commanded_velocity_.e - velocity_.e;
    const double dn = commanded_velocity_.n - velocity_.n;
    const double gap = std::sqrt(de * de + dn * dn);
    const double max_dv = kMaxAccelerationMps2 * dt_s;
    if (gap > 1e-9) {
        const double scale = std::min(1.0, max_dv / gap);
        velocity_.e += de * scale;
        velocity_.n += dn * scale;
    }
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

    // Emulated hover/flight tilt: a multirotor leans into the RELATIVE wind
    // to hold position/attitude. The lean direction is the bearing the
    // relative wind comes FROM; magnitude from the same drag balance the
    // wind estimator inverts: tan(theta) = rho*CdA*V_rel^2 / (2*m*g).
    const core::EnuVector wind = WindAtHeight(position_.u);
    const double vrel_e = wind.e - velocity_.e;
    const double vrel_n = wind.n - velocity_.n;
    const double vrel_sq = vrel_e * vrel_e + vrel_n * vrel_n;
    const double rho = core::atmosphere::DensityKgM3(
        core::atmosphere::PressurePa(position_.u + kOriginLla.alt_m_hae), 15.0);
    const double airborne_mass_kg =
        config_.aircraft_mass_kg + config_.onboard_equipment_kg + config_.payload_mass_kg;
    const double theta =
        std::atan(rho * config_.cda_airframe * vrel_sq / (2.0 * airborne_mass_kg * core::ballistics::kGravity));
    const double from_bearing = core::geodesy::WrapAngleRad(std::atan2(-vrel_e, -vrel_n));
    const double delta = from_bearing - heading_rad_;
    state.pitch_rad = -theta * std::cos(delta);  // + = nose up
    state.roll_rad = theta * std::sin(delta);    // + = right wing down

    state.yaw_rad = core::geodesy::WrapAngleRad(core::kPi / 2.0 - heading_rad_);
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

double SimWorld::WindSpeedAtHeight(double height_agl_m) const {
    double v_ref = config_.wind_mean_mps;
    if (config_.wind_gust_amplitude_mps > 0.0) {
        v_ref += config_.wind_gust_amplitude_mps *
                 std::sin(2.0 * core::kPi * config_.wind_gust_band_hz * t_s_);
    }
    v_ref = std::max(0.0, v_ref);
    if (config_.type == ScenarioType::kWindShear) {
        // Deliberately mismatched with the estimator's model: constant
        // speed with height (and the direction backs, below).
        return v_ref;
    }
    // Truth = the same power law the guidance model assumes, referenced to
    // the scenario AGL so the two agree nominally.
    return core::wind::SpeedAtHeight(v_ref, config_.wind_reference_height_m, height_agl_m,
                                     kWindAlpha);
}

core::EnuVector SimWorld::WindAtHeight(double height_agl_m) const {
    double from_deg = config_.wind_from_deg;
    if (config_.type == ScenarioType::kWindShear) {
        // Wind direction backs with height to exercise shear handling.
        from_deg += height_agl_m * 0.3;
    }
    return core::wind::ToVector(WindSpeedAtHeight(height_agl_m), from_deg * core::kPi / 180.0);
}

void SimWorld::SimulateTruthDrop(const core::PayloadProfile& payload, const core::EnuPoint& aim_enu) {
    core::ballistics::ForwardInputs fwd;
    fwd.mass_kg = payload.mass_kg;
    fwd.cda_at_time = core::ballistics::MakeCdaProfile(payload);
    fwd.release_height_agl_m = position_.u - aim_enu.u;
    fwd.initial_velocity = velocity_;
    fwd.wind_at_height = [this](double h) { return WindAtHeight(h); };
    fwd.air_density_kg_m3 = core::atmosphere::DensityKgM3(
        core::atmosphere::PressurePa(position_.u + kOriginLla.alt_m_hae), 15.0);
    const core::BallisticResult result = core::ballistics::IntegrateForward(fwd);
    if (!result.converged) return;
    truth_impact_ = core::EnuPoint{position_.e + result.impact_displacement.e,
                                   position_.n + result.impact_displacement.n, aim_enu.u};
    truth_impact_valid_ = true;
}

}  // namespace droppoint::sim
