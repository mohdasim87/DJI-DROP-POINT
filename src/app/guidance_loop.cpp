#include "guidance_loop.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "core/atmosphere.h"
#include "core/dispersion.h"
#include "core/wind.h"

namespace droppoint::app {

namespace {

// Signed-safe age in seconds between two same-domain microsecond
// timestamps; negative ages (timestamp jitter running ahead) clamp to 0.
double AgeSeconds(std::uint64_t now_us, std::uint64_t then_us) {
    const std::int64_t diff = static_cast<std::int64_t>(now_us) - static_cast<std::int64_t>(then_us);
    return diff > 0 ? static_cast<double>(diff) / 1e6 : 0.0;
}

const char* ElevationSourceTag(core::ElevationSource source) {
    switch (source) {
        case core::ElevationSource::kLrfCorrected: return "LRF";
        case core::ElevationSource::kHomeTiedDem: return "DEM+HOME";
        case core::ElevationSource::kFlatAtHome: return "FLAT";
        case core::ElevationSource::kRawDem:
        case core::ElevationSource::kUnknown:
        default: return "DEM";
    }
}

}  // namespace

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
      release_(release),
      target_sources_(std::move(target_sources)),
      config_(std::move(config)),
      release_manager_(release, audio, mission_),
      wind_estimator_(config_.wind),
      active_payload_(config_.payload),
      temperature_c_(config_.ambient_temperature_c) {
    wind_estimator_.SetTotalMassKg(config_.aircraft_mass_kg + config_.onboard_equipment_kg +
                                   active_payload_.mass_kg);
}

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
                                                    double acceleration_mps2,
                                                    const core::WindSample& wind, bool wind_valid,
                                                    double gust_sigma_mps) const {
    core::gating::Inputs in;
    const core::Target& target = mission_.CurrentTarget();
    in.target_latched = target.valid;
    in.target_confirmed = target.confirmed;
    in.target_age_s = target.valid ? AgeSeconds(solution.timestamp_us, target.designated_at_us) : 1e9;
    in.depression_angle_rad = target.depression_angle_rad;

    const hal::GnssStatus gnss = telemetry_.GetGnssStatus();
    in.gnss_satellites = gnss.satellite_count;
    in.rtk_status = gnss.rtk_status == hal::RtkFixStatus::kFix     ? core::gating::RtkStatus::kFix
                    : gnss.rtk_status == hal::RtkFixStatus::kFloat ? core::gating::RtkStatus::kFloat
                                                                    : core::gating::RtkStatus::kNotAvailable;

    in.telemetry_age_s = telemetry_.GetTelemetryAgeUs() / 1e6;
    in.acceleration_mps2 = acceleration_mps2;
    in.agl_m = aircraft.position.u - target.enu.u;  // height above the target's ground elevation
    in.terrain_elevation_available = target.valid;  // a successful designation implies a DEM hit
    in.inverse_solver_converged = solution.has_solution;
    in.predicted_impact_divergence_m = solution.miss_distance_m;
    in.target_elevation_source = target.elevation_source;
    in.wind_valid = wind_valid;
    in.wind_model_calibrated = config_.wind_model_calibrated;
    in.gust_sigma_mps = gust_sigma_mps;
    in.payload_calibrated = active_payload_.calibrated;

    // Crosswind gate: on a run-in only the cross-track component of the wind
    // pushes the impact off the line; in a hover the payload drifts with the
    // full wind, so the total speed applies (spec 7).
    if (solution.hover_mode || !solution.forward_valid) {
        in.crosswind_mps = wind.speed_mps;
    } else {
        const double speed = std::sqrt(aircraft.velocity.e * aircraft.velocity.e +
                                        aircraft.velocity.n * aircraft.velocity.n);
        if (speed > 1e-6) {
            const double te = aircraft.velocity.e / speed;
            const double tn = aircraft.velocity.n / speed;
            const core::EnuVector w = core::wind::ToVector(wind.speed_mps, wind.from_bearing_rad);
            in.crosswind_mps = std::fabs(w.e * tn - w.n * te);
        } else {
            in.crosswind_mps = wind.speed_mps;
        }
    }
    return in;
}

std::string GuidanceLoop::BuildFloatingWindowText(const core::Solution& solution,
                                                   const core::gating::Evaluation& gates,
                                                   const core::AircraftState& aircraft) const {
    const core::Target& target = mission_.CurrentTarget();
    char buf[256];
    std::string text;

    if (!target.valid) {
        text = "-- NO TARGET\n   AIM AND PRESS SET TARGET";
    } else if (gates.any_hard_tripped) {
        std::snprintf(buf, sizeof(buf), "XX HOLD - %s", gates.primary_hold_reason.c_str());
        text = buf;
    } else {
        switch (mission_.Id()) {
            case MissionStateId::kReleased:
                text = ">> RELEASED - RE-ARM FOR NEXT DROP";
                break;
            case MissionStateId::kCountdown:
                if (solution.time_to_release_s <= 0.0) {
                    text = ">> DROP";
                } else {
                    std::snprintf(buf, sizeof(buf), ">> DROP IN %d",
                                  static_cast<int>(solution.time_to_release_s + 0.999));
                    text = buf;
                }
                break;
            case MissionStateId::kArmed:
                if (solution.hover_mode) {
                    if (solution.release_window_open) {
                        if (release_.IsOperatorActuated()) {
                            text = ">> DROP OK - PRESS RELEASE ON DROP KIT";
                        } else {
                            text = ">> DROP OK - PRESS RELEASE";
                        }
                    } else {
                        std::snprintf(buf, sizeof(buf), ">> MOVE %.1fm BRG %.0f",
                                      solution.steer_distance_m,
                                      solution.steer_bearing_rad * 180.0 / core::kPi);
                        text = buf;
                    }
                } else {
                    const char* mode = mission_.Mode() == ReleaseMode::kAuto ? "AUTO" : "MANUAL";
                    std::snprintf(buf, sizeof(buf), ">> %s T-%.1fs\n   XTRK %.1fm", mode,
                                  solution.time_to_release_s, solution.miss_distance_m);
                    text = buf;
                }
                break;
            case MissionStateId::kConfirmed: {
                std::snprintf(buf, sizeof(buf), ">> PRESS ARM\n   MISS %.1fm CEP %.1fm",
                              solution.miss_distance_m, solution.dispersion.p50_radial_m);
                text = buf;
                break;
            }
            default: {
                // Latched but unconfirmed: range/bearing from the aircraft,
                // ground elevation and its source, then the next action.
                const double range =
                    core::geodesy::HorizontalRange(aircraft.position, target.enu);
                const double bearing_deg =
                    core::geodesy::BearingRad(aircraft.position, target.enu) * 180.0 / core::kPi;
                const double ground_elev_m = target.enu.u + config_.origin.lla.alt_m_hae;
                std::snprintf(buf, sizeof(buf), ">> TGT %.0fm BRG %.0f\n   ELEV %.0fm %s - PRESS CONFIRM",
                              range, bearing_deg, ground_elev_m,
                              ElevationSourceTag(target.elevation_source));
                text = buf;
                break;
            }
        }
    }

    // First tripped soft gate rides along as a "!!" line (spec 9), except on
    // the no-target and hard-hold screens which already carry their reason.
    if (target.valid && !gates.any_hard_tripped) {
        for (const auto& r : gates.results) {
            if (r.tripped && r.severity == core::gating::GateSeverity::kSoft) {
                text += "\n!! " + r.message;
                break;
            }
        }
    }

    // ASCII-only, hard 255-byte budget for the floating window channel.
    if (text.size() > hal::kFloatingWindowMaxBytes) {
        text.resize(hal::kFloatingWindowMaxBytes);
    }
    return text;
}

void GuidanceLoop::AppendDropLogEntry(const core::AircraftState& aircraft, const core::Solution& solution,
                                       const core::gating::Evaluation& gates) {
    if (config_.drop_log == nullptr) return;

    std::string gate_summary = "all-clear";
    for (const auto& r : gates.results) {
        if (r.tripped) {
            gate_summary = r.severity == core::gating::GateSeverity::kHard ? "hard:" : "soft:";
            gate_summary += r.message;
            break;
        }
    }

    util::DropLogEntry entry;
    entry.timestamp_us = solution.timestamp_us;
    entry.release_mode = mission_.Mode() == ReleaseMode::kAuto ? "auto" : "manual";
    entry.payload_name = active_payload_.name;
    entry.release_point = aircraft.position;
    entry.predicted_impact = solution.has_solution ? solution.predicted_impact_point
                                                   : solution.impact_if_released_now;
    entry.latched_target = mission_.CurrentTarget().enu;
    entry.p50_dispersion_m = solution.dispersion.p50_radial_m;
    entry.p90_dispersion_m = solution.dispersion.p90_radial_m;
    entry.gate_states_summary = gate_summary;
    config_.drop_log->Append(entry);
}

core::Solution GuidanceLoop::Tick(const CycleInputs& in) {
    const core::AircraftState aircraft = telemetry_.GetAircraftState();
    const core::GimbalState gimbal_state = gimbal_.GetAttitude();

    // Acceleration: finite difference of successive telemetry velocities,
    // EMA-smoothed with tau 0.5 s, then max()ed with any externally supplied
    // estimate.
    double acceleration = in.acceleration_mps2;
    if (aircraft.valid) {
        if (have_prev_velocity_ && in.now_us > prev_velocity_us_) {
            const double dt = (in.now_us - prev_velocity_us_) / 1e6;
            const double de = aircraft.velocity.e - prev_velocity_.e;
            const double dn = aircraft.velocity.n - prev_velocity_.n;
            const double du = aircraft.velocity.u - prev_velocity_.u;
            const double raw = std::sqrt(de * de + dn * dn + du * du) / dt;
            const double alpha = 1.0 - std::exp(-dt / 0.5);
            accel_ema_mps2_ += alpha * (raw - accel_ema_mps2_);
        }
        prev_velocity_ = aircraft.velocity;
        prev_velocity_us_ = in.now_us;
        have_prev_velocity_ = true;
        acceleration = std::max(acceleration, accel_ema_mps2_);
    }

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

    // Mode / payload / temperature changes are ignored while the mission is
    // armed, counting down, or in the post-fire hold.
    const MissionStateId id = mission_.Id();
    const bool changes_locked = id == MissionStateId::kArmed || id == MissionStateId::kCountdown ||
                                id == MissionStateId::kReleased;
    if (!changes_locked) {
        if (in.ambient_temperature_c.has_value() && std::isfinite(*in.ambient_temperature_c)) {
            temperature_c_ = *in.ambient_temperature_c;
        }
        if (in.requested_wind_alpha.has_value() && std::isfinite(*in.requested_wind_alpha) &&
            *in.requested_wind_alpha > 0.0) {
            config_.wind_alpha = *in.requested_wind_alpha;
        }
        if (in.requested_surface_index.has_value()) {
            surface_index_ = *in.requested_surface_index;
        }
        if (in.requested_payload_index.has_value()) {
            const int idx = *in.requested_payload_index;
            if (idx >= 0 && idx < static_cast<int>(config_.payloads.size())) {
                active_payload_ = config_.payloads[static_cast<std::size_t>(idx)];
                active_payload_index_ = idx;
                wind_estimator_.SetTotalMassKg(config_.aircraft_mass_kg +
                                               config_.onboard_equipment_kg + active_payload_.mass_kg);
            }
        }
        if (in.requested_mode.has_value()) {
            std::string reject_reason;
            mission_.TrySetMode(*in.requested_mode, !release_.IsOperatorActuated(), &reject_reason);
        }
    }

    UpdateDatumBiasFromRangefinder(in.now_us);

    const double density = core::atmosphere::DensityKgM3(
        core::atmosphere::PressurePa(aircraft.position.u + config_.origin.lla.alt_m_hae),
        temperature_c_);

    // Wind: prefer a telemetry-topic sample when one exists; otherwise fall
    // back to the hover-attitude estimator (the only wind source on current
    // aircraft). The estimator is fed every cycle regardless.
    const core::Target& target = mission_.CurrentTarget();
    const core::WindSample telemetry_wind = telemetry_.GetWind();
    const bool telemetry_wind_usable = telemetry_wind.valid && telemetry_wind.from_telemetry_topic;

    core::WindSample wind;
    double wind_z_ref_m = 2.0;
    bool wind_valid = false;
    if (telemetry_wind_usable) {
        wind = telemetry_wind;
        wind_valid = true;
        wind_z_ref_m = target.valid ? std::max(0.5, aircraft.position.u - target.enu.u) : 2.0;
    } else {
        wind_estimator_.Update(aircraft, density, acceleration, in.now_us);
        wind = wind_estimator_.Current(in.now_us);
        wind_valid = wind.valid;
        if (target.valid) {
            wind_z_ref_m = std::max(2.0, wind_estimator_.ReferenceU() - target.enu.u);
        }
    }
    const double gust_sigma_mps = wind_estimator_.GustSigmaMps();

    core::Solution solution;
    solution.timestamp_us = in.now_us;

    const double ground_speed =
        std::sqrt(aircraft.velocity.e * aircraft.velocity.e + aircraft.velocity.n * aircraft.velocity.n);
    const bool run_in_mode = aircraft.valid && ground_speed >= config_.min_run_in_speed_mps;
    solution.hover_mode = !run_in_mode;

    if (target.valid && aircraft.valid) {
        const double release_agl = aircraft.position.u - target.enu.u;
        auto wind_fn = [this, &wind, wind_z_ref_m](double h) {
            const double speed =
                core::wind::SpeedAtHeight(wind.speed_mps, wind_z_ref_m, h, config_.wind_alpha);
            return core::wind::ToVector(speed, wind.from_bearing_rad);
        };
        const auto cda_fn = core::ballistics::MakeCdaProfile(active_payload_);

        // Forward "impact if released now" — always computed so the loop can
        // steer and (in a hover) release on it directly.
        core::ballistics::ForwardInputs fwd;
        fwd.mass_kg = active_payload_.mass_kg;
        fwd.cda_at_time = cda_fn;
        fwd.release_height_agl_m = release_agl;
        fwd.initial_velocity = aircraft.velocity;
        fwd.wind_at_height = wind_fn;
        fwd.air_density_kg_m3 = density;
        const core::BallisticResult forward = core::ballistics::IntegrateForward(fwd);
        solution.forward_valid = forward.converged;
        if (forward.converged) {
            solution.impact_if_released_now =
                core::EnuPoint{aircraft.position.e + forward.impact_displacement.e,
                               aircraft.position.n + forward.impact_displacement.n, target.enu.u};
            solution.miss_if_released_now_m =
                core::geodesy::HorizontalRange(solution.impact_if_released_now, target.enu);
            solution.steer_distance_m = solution.miss_if_released_now_m;
            solution.steer_bearing_rad =
                core::geodesy::BearingRad(solution.impact_if_released_now, target.enu);
        }

        if (run_in_mode) {
            core::ballistics::InverseInputs inv;
            inv.mass_kg = active_payload_.mass_kg;
            inv.cda_at_time = cda_fn;
            inv.aircraft_position = aircraft.position;
            inv.aircraft_velocity = aircraft.velocity;
            inv.target = target.enu;
            inv.wind_at_height = wind_fn;
            inv.air_density_kg_m3 = density;

            const auto inverse_result = core::ballistics::SolveInverse(inv);
            solution.has_solution = inverse_result.converged;
            if (inverse_result.converged) {
                solution.time_to_release_s = inverse_result.release_along_track_distance_m / ground_speed;
                solution.ballistics = inverse_result.ballistics_at_release;
                solution.predicted_impact_point = core::EnuPoint{
                    inverse_result.release_point.e + inverse_result.ballistics_at_release.impact_displacement.e,
                    inverse_result.release_point.n + inverse_result.ballistics_at_release.impact_displacement.n,
                    target.enu.u};
                solution.miss_distance_m =
                    core::geodesy::HorizontalRange(solution.predicted_impact_point, target.enu);
                solution.release_window_open =
                    solution.miss_distance_m <= active_payload_.target_radius_m;
            } else {
                solution.no_solution_reason = "inverse solver did not converge";
            }
        } else {
            // Hover drop: the forward prediction IS the solution. The
            // inverse solver cannot converge at ~zero ground speed.
            solution.has_solution = solution.forward_valid;
            solution.miss_distance_m = solution.miss_if_released_now_m;
            solution.release_window_open =
                solution.forward_valid &&
                solution.miss_if_released_now_m <= active_payload_.target_radius_m;
            solution.time_to_release_s = 0.0;
            solution.ballistics = forward;
            if (solution.forward_valid) {
                solution.predicted_impact_point = solution.impact_if_released_now;
            } else {
                solution.no_solution_reason = "forward integration did not converge";
            }
        }

        // Monte Carlo dispersion alongside the deterministic solve, rate-
        // limited: 200 RK4 integrations per run is too expensive for every
        // 10 Hz cycle (spec 6.6).
        if (solution.forward_valid && config_.monte_carlo_samples > 0 &&
            in.now_us - last_monte_carlo_us_ >=
                static_cast<std::uint64_t>(1e6 / config_.monte_carlo_rate_hz)) {
            core::dispersion::MonteCarloInputs mc;
            mc.baseline = fwd;
            mc.wind_speed_mps = wind.speed_mps;
            mc.wind_from_bearing_rad = wind.from_bearing_rad;
            mc.wind_gust_sigma_mps = gust_sigma_mps;
            mc.ground_speed_mps = ground_speed;
            mc.wind_reference_height_m = wind_z_ref_m;
            mc.wind_alpha = config_.wind_alpha;
            mc.samples = config_.monte_carlo_samples;
            last_dispersion_ = core::dispersion::RunMonteCarlo(mc);
            last_monte_carlo_us_ = in.now_us;
        }
        solution.dispersion = last_dispersion_;
    } else {
        solution.no_solution_reason = target.valid ? "no valid aircraft state" : "no target latched";
    }

    const core::gating::Inputs gate_inputs =
        BuildGateInputs(aircraft, solution, acceleration, wind, wind_valid, gust_sigma_mps);
    core::gating::Thresholds thresholds = config_.gate_thresholds;
    thresholds.crosswind_limit_mps = active_payload_.crosswind_limit_mps;
    thresholds.min_release_agl_m = active_payload_.min_release_agl_m;
    thresholds.max_release_agl_m = active_payload_.max_release_agl_m;
    thresholds.rtk_required = config_.rtk_required;
    const core::gating::Evaluation gates = core::gating::Evaluate(gate_inputs, thresholds);
    last_gates_ = gates;

    if (in.arm_pressed) {
        std::string reject_reason;
        mission_.TryArm(in.now_us, !gates.any_hard_tripped, &reject_reason);
    }

    // Release manager first: it must be able to de-arm the mechanism the
    // same cycle a gate trips, before MissionState::Tick does any expiry
    // bookkeeping.
    ReleaseManagerInputs rm_in;
    rm_in.now_us = in.now_us;
    rm_in.time_to_release_s = solution.time_to_release_s;
    rm_in.mechanism_delay_s = active_payload_.mechanism_delay_s;
    rm_in.target_confirmed_age_s =
        target.confirmed ? AgeSeconds(in.now_us, target.confirmed_at_us) : 1e9;
    rm_in.gates_pass = !gates.any_hard_tripped;
    rm_in.trip_reason = gates.primary_hold_reason;
    rm_in.manual_release_pressed = in.manual_release_pressed;
    rm_in.hardware_abort_pressed = in.hardware_abort_pressed;
    rm_in.disarm_pressed = in.disarm_pressed;
    rm_in.no_drop_polygon_violation = gate_inputs.predicted_impact_in_no_drop_polygon;
    rm_in.run_in_mode = run_in_mode;
    rm_in.release_window_open = solution.release_window_open;
    rm_in.release_hold_s = config_.release_hold_s;
    rm_in.auto_release_allowed = !release_.IsOperatorActuated();
    release_manager_.Update(rm_in);

    mission_.Tick(in.now_us);

    if (release_manager_.JustFired()) {
        AppendDropLogEntry(aircraft, solution, gates);
    }

    // Floating window: floating_window_hz normally, up to 5 Hz during
    // COUNTDOWN, and immediately on any mission-state change.
    const std::uint64_t min_interval_us =
        mission_.Id() == MissionStateId::kCountdown
            ? 200000
            : static_cast<std::uint64_t>(1e6 / config_.floating_window_hz);
    const bool state_changed = !have_display_state_ || mission_.Id() != last_display_state_;
    if (state_changed || in.now_us - last_display_us_ >= min_interval_us) {
        display_.ShowMessage(BuildFloatingWindowText(solution, gates, aircraft));
        last_display_us_ = in.now_us;
        last_display_state_ = mission_.Id();
        have_display_state_ = true;
    }

    std::vector<hal::Marker> markers;
    if (target.valid) markers.push_back(hal::Marker{hal::MarkerKind::kLatchedTarget, true, 0, 0});
    if (solution.has_solution) markers.push_back(hal::Marker{hal::MarkerKind::kPredictedImpact, true, 0, 0});
    liveview_.SendMarkers(markers);

    (void)gimbal_state;
    return solution;
}

}  // namespace droppoint::app
