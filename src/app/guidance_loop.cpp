#include "guidance_loop.h"

#include <cmath>
#include <sstream>

#include "core/atmosphere.h"
#include "core/wind.h"

namespace droppoint::app {

GuidanceLoop::GuidanceLoop(hal::ITelemetry& telemetry, hal::IGimbal& gimbal, hal::IRangefinder& rangefinder,
                            hal::IDisplay& display, hal::IAudio& audio, hal::ILiveview& liveview,
                            hal::IRelease& release, std::vector<hal::ITargetSource*> target_sources,
                            GuidanceLoopConfig config)
    : telemetry_(telemetry),
      gimbal_(gimbal),
      rangefinder_(rangefinder),
      display_(display),
      audio_(audio),
      liveview_(liveview),
      target_sources_(std::move(target_sources)),
      config_(std::move(config)),
      release_manager_(release, audio, mission_) {}

void GuidanceLoop::UpdateDatumBiasFromRangefinder(std::uint64_t now_us) {
    const auto reading = rangefinder_.GetLatestReading();
    if (!reading.has_value() || !reading->valid) return;

    const auto dem = config_.dem_fn(reading->geodetic.lat_deg, reading->geodetic.lon_deg);
    if (!dem.has_value()) return;

    const core::EnuPoint p_lrf = core::geodesy::LlaToEnu(reading->geodetic, config_.origin);
    const core::AircraftState aircraft = telemetry_.GetAircraftState();
    datum_bias_.Update(p_lrf.u + config_.origin.lla.alt_m_hae, *dem, now_us, aircraft.position);
}

core::gating::Inputs GuidanceLoop::BuildGateInputs(const core::AircraftState& aircraft,
                                                    const core::Solution& solution,
                                                    double acceleration_mps2) const {
    core::gating::Inputs in;
    const core::Target& target = mission_.CurrentTarget();
    in.target_latched = target.valid;
    in.target_confirmed = target.confirmed;
    in.target_age_s = target.valid ? (aircraft.timestamp_us - target.designated_at_us) / 1e6 : 1e9;
    in.depression_angle_rad = target.depression_angle_rad;

    const hal::GnssStatus gnss = const_cast<hal::ITelemetry&>(telemetry_).GetGnssStatus();
    in.gnss_satellites = gnss.satellite_count;
    in.rtk_status = gnss.rtk_status == hal::RtkFixStatus::kFix     ? core::gating::RtkStatus::kFix
                    : gnss.rtk_status == hal::RtkFixStatus::kFloat ? core::gating::RtkStatus::kFloat
                                                                    : core::gating::RtkStatus::kNotAvailable;

    in.telemetry_age_s = const_cast<hal::ITelemetry&>(telemetry_).GetTelemetryAgeUs() / 1e6;
    in.acceleration_mps2 = acceleration_mps2;
    in.agl_m = aircraft.position.u - target.enu.u;  // height above the target's ground elevation
    in.terrain_elevation_available = target.valid;  // a successful designation implies a DEM hit
    in.inverse_solver_converged = solution.has_solution;
    in.predicted_impact_divergence_m = solution.miss_distance_m;
    in.target_elevation_source = target.elevation_source;

    const core::WindSample wind = const_cast<hal::ITelemetry&>(telemetry_).GetWind();
    in.crosswind_mps = wind.speed_mps;  // simplified: full cross/along-track decomposition lives in dispersion
    in.gust_sigma_mps = 0.0;            // populated by the dispersion module's rolling window in a full build
    in.payload_calibrated = config_.payload.calibrated;
    return in;
}

std::string GuidanceLoop::BuildFloatingWindowText(const core::Solution& solution,
                                                   const core::gating::Evaluation& gates) const {
    std::ostringstream oss;
    if (!mission_.CurrentTarget().valid) {
        oss << "-- NO TARGET\n   AIM AND PRESS SET TARGET";
        return oss.str();
    }
    if (gates.any_hard_tripped) {
        oss << "XX HOLD - " << gates.primary_hold_reason;
        return oss.str();
    }
    const char* mode = mission_.Mode() == ReleaseMode::kAuto ? "AUTO" : "MANUAL";
    oss << ">> DROP OK  " << mode << "   T-" << solution.time_to_release_s << "s\n"
        << "   MISS " << solution.miss_distance_m << "m";
    return oss.str();
}

core::Solution GuidanceLoop::Tick(const CycleInputs& in) {
    const core::AircraftState aircraft = telemetry_.GetAircraftState();
    const core::GimbalState gimbal_state = gimbal_.GetAttitude();

    if (in.clear_target_pressed) mission_.OnClearTarget();
    if (in.confirm_target_pressed) mission_.OnConfirmTarget(in.now_us);

    if (in.set_target_pressed) {
        rangefinder_.RequestMeasurement();
        for (hal::ITargetSource* source : target_sources_) {
            if (!source->IsAvailable()) continue;
            const auto outcome = source->TryDesignate(hal::DesignationRequest{true, in.now_us});
            if (!outcome.has_value()) continue;
            if (outcome->succeeded) {
                mission_.OnTargetDesignated(outcome->target);
            }
            break;  // first available source that attempted a designation wins, per spec 3 priority order
        }
    }

    UpdateDatumBiasFromRangefinder(in.now_us);

    core::Solution solution;
    solution.timestamp_us = in.now_us;

    const core::Target& target = mission_.CurrentTarget();
    if (target.valid) {
        const double temperature_c = 15.0;  // TODO(config): wire the TEMP widget input through
        const double density = core::atmosphere::DensityKgM3(
            core::atmosphere::PressurePa(aircraft.position.u + config_.origin.lla.alt_m_hae), temperature_c);
        const core::WindSample wind = telemetry_.GetWind();
        const double release_agl = aircraft.position.u - target.enu.u;

        auto wind_fn = [this, &wind, release_agl](double h) {
            const double speed = core::wind::SpeedAtHeight(wind.speed_mps, release_agl, h, config_.wind_alpha);
            return core::wind::ToVector(speed, wind.from_bearing_rad);
        };

        core::ballistics::InverseInputs inv;
        inv.mass_kg = config_.payload.mass_kg;
        inv.cda_at_time = core::ballistics::MakeCdaProfile(config_.payload);
        inv.aircraft_position = aircraft.position;
        inv.aircraft_velocity = aircraft.velocity;
        inv.target = target.enu;
        inv.wind_at_height = wind_fn;
        inv.air_density_kg_m3 = density;

        const auto inverse_result = core::ballistics::SolveInverse(inv);
        solution.has_solution = inverse_result.converged;
        if (inverse_result.converged) {
            const double speed = std::sqrt(aircraft.velocity.e * aircraft.velocity.e +
                                            aircraft.velocity.n * aircraft.velocity.n);
            solution.time_to_release_s = speed > 1e-3 ? inverse_result.release_along_track_distance_m / speed : 0.0;
            solution.ballistics = inverse_result.ballistics_at_release;
            solution.predicted_impact_point =
                core::EnuPoint{inverse_result.release_point.e + inverse_result.ballistics_at_release.impact_displacement.e,
                               inverse_result.release_point.n + inverse_result.ballistics_at_release.impact_displacement.n,
                               target.enu.u};
            solution.miss_distance_m = core::geodesy::HorizontalRange(solution.predicted_impact_point, target.enu);
        } else {
            solution.no_solution_reason = "inverse solver did not converge";
        }
    } else {
        solution.no_solution_reason = "no target latched";
    }

    const core::gating::Inputs gate_inputs = BuildGateInputs(aircraft, solution, in.acceleration_mps2);
    core::gating::Thresholds thresholds = config_.gate_thresholds;
    thresholds.crosswind_limit_mps = config_.payload.crosswind_limit_mps;
    thresholds.min_release_agl_m = config_.payload.min_release_agl_m;
    thresholds.rtk_required = config_.rtk_required;
    const core::gating::Evaluation gates = core::gating::Evaluate(gate_inputs, thresholds);

    if (in.arm_pressed) {
        std::string reject_reason;
        mission_.TryArm(in.now_us, !gates.any_hard_tripped, &reject_reason);
    }
    mission_.Tick(in.now_us, !gates.any_hard_tripped, gates.primary_hold_reason);

    ReleaseManagerInputs rm_in;
    rm_in.now_us = in.now_us;
    rm_in.time_to_release_s = solution.time_to_release_s;
    rm_in.mechanism_delay_s = config_.payload.mechanism_delay_s;
    rm_in.target_confirmed_age_s = target.confirmed ? (in.now_us - target.confirmed_at_us) / 1e6 : 1e9;
    rm_in.gates_pass = !gates.any_hard_tripped;
    rm_in.trip_reason = gates.primary_hold_reason;
    rm_in.manual_release_pressed = in.manual_release_pressed;
    rm_in.hardware_abort_pressed = in.hardware_abort_pressed;
    release_manager_.Update(rm_in);

    if (release_manager_.JustFired()) {
        mission_.ReturnToIdleAfterRelease();
    }

    display_.ShowMessage(BuildFloatingWindowText(solution, gates));

    std::vector<hal::Marker> markers;
    if (target.valid) markers.push_back(hal::Marker{hal::MarkerKind::kLatchedTarget, true, 0, 0});
    if (solution.has_solution) markers.push_back(hal::Marker{hal::MarkerKind::kPredictedImpact, true, 0, 0});
    liveview_.SendMarkers(markers);

    (void)gimbal_state;
    return solution;
}

}  // namespace droppoint::app
