// app/guidance_loop.h
//
// The 10 Hz guidance loop (spec section 6.4/6.7/9): pulls telemetry and
// gimbal attitude, runs the forward/inverse ballistics solve and gating,
// drives the floating window/audio/liveview HAL interfaces, and feeds the
// release manager. This is the top-level orchestration; all the actual
// math lives in core/.
//
// Two drop geometries are supported: run-in drops (inverse solver computes
// the release point along the current track) and hover drops (the forward
// "impact if released now" prediction is steered onto the target; the
// inverse solver cannot converge at ~zero ground speed).
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "core/ballistics.h"
#include "core/gating.h"
#include "core/geodesy.h"
#include "core/solution.h"
#include "core/terrain.h"
#include "hal/i_audio.h"
#include "hal/i_display.h"
#include "hal/i_gimbal.h"
#include "hal/i_liveview.h"
#include "hal/i_rangefinder.h"
#include "hal/i_release.h"
#include "hal/i_target_source.h"
#include "hal/i_telemetry.h"
#include "mission_state.h"
#include "release_manager.h"
#include "util/drop_log.h"
#include "wind_estimator.h"

namespace droppoint::app {

struct GuidanceLoopConfig {
    core::gating::Thresholds gate_thresholds;
    core::PayloadProfile payload;                  // the active payload at startup
    std::vector<core::PayloadProfile> payloads;    // selectable via CycleInputs::requested_payload_index
    core::geodesy::EnuOrigin origin;
    core::terrain::DemSampleFn dem_fn;
    double wind_alpha = 0.14;
    bool rtk_required = false;
    double ambient_temperature_c = 15.0;
    double min_run_in_speed_mps = 1.0;  // below this ground speed the loop is in hover mode
    double release_hold_s = 1.5;        // post-fire hold before the mechanism is driven safe
    WindEstimatorConfig wind;
    bool wind_model_calibrated = false;
    util::DropLog* drop_log = nullptr;  // optional; appended on every release when set
    int monte_carlo_samples = 200;
    double monte_carlo_rate_hz = 1.0;   // MC dispersion update rate (expensive; not per-tick)
    double floating_window_hz = 2.0;
    double aircraft_mass_kg = 0.0;      // without payload; wind estimator mass =
    double onboard_equipment_kg = 0.0;  //   aircraft + equipment + active payload
};

class GuidanceLoop {
public:
    GuidanceLoop(hal::ITelemetry& telemetry, hal::IGimbal& gimbal, hal::IRangefinder& rangefinder,
                 hal::IDisplay& display, hal::IAudio& audio, hal::ILiveview& liveview,
                 hal::IRelease& release, std::vector<hal::ITargetSource*> target_sources,
                 GuidanceLoopConfig config);

    // Runs one cycle. `manual_release_pressed`/`hardware_abort_pressed`/etc.
    // come from polled RC button events or widget presses, resolved by
    // main's input dispatch. Mode, payload, and temperature changes are
    // ignored while ARMED/COUNTDOWN/RELEASED.
    struct CycleInputs {
        std::uint64_t now_us = 0;
        bool set_target_pressed = false;
        bool confirm_target_pressed = false;
        bool clear_target_pressed = false;
        bool arm_pressed = false;
        bool manual_release_pressed = false;
        bool hardware_abort_pressed = false;
        bool disarm_pressed = false;
        double acceleration_mps2 = 0.0;  // additional external estimate (e.g. IMU), max()ed with the finite-difference value
        std::optional<ReleaseMode> requested_mode;
        std::optional<int> requested_payload_index;
        std::optional<double> ambient_temperature_c;
        std::optional<double> requested_wind_alpha;  // surface-type override, see SURFACE widget
        std::optional<int> requested_surface_index;  // SURFACE list index (reported back to Pilot 2)
    };
    core::Solution Tick(const CycleInputs& in);

    MissionState& Mission() { return mission_; }

    // Live values the Pilot 2 widgets report back (read-only).
    int ActivePayloadIndex() const { return active_payload_index_; }
    double AmbientTemperatureC() const { return temperature_c_; }
    int SurfaceIndex() const { return surface_index_; }

private:
    void UpdateDatumBiasFromRangefinder(std::uint64_t now_us);
    core::gating::Inputs BuildGateInputs(const core::AircraftState& aircraft, const core::Solution& solution,
                                          double acceleration_mps2, const core::WindSample& wind,
                                          bool wind_valid, double gust_sigma_mps) const;
    std::string BuildFloatingWindowText(const core::Solution& solution, const core::gating::Evaluation& gates,
                                         const core::AircraftState& aircraft) const;
    void AppendDropLogEntry(const core::AircraftState& aircraft, const core::Solution& solution,
                            const core::gating::Evaluation& gates);

    hal::ITelemetry& telemetry_;
    hal::IGimbal& gimbal_;
    hal::IRangefinder& rangefinder_;
    hal::IDisplay& display_;
    hal::IAudio& audio_;
    hal::ILiveview& liveview_;
    hal::IRelease& release_;
    std::vector<hal::ITargetSource*> target_sources_;
    GuidanceLoopConfig config_;

    MissionState mission_;
    ReleaseManager release_manager_;
    core::terrain::DatumBias datum_bias_;
    WindEstimator wind_estimator_;

    core::PayloadProfile active_payload_;
    int active_payload_index_ = 0;
    int surface_index_ = 1;  // kOpenCountry — matches the default wind_alpha
    double temperature_c_ = 15.0;

    // Finite-difference acceleration estimate (EMA, tau 0.5 s) computed from
    // successive telemetry velocities.
    bool have_prev_velocity_ = false;
    core::EnuVector prev_velocity_{};
    std::uint64_t prev_velocity_us_ = 0;
    double accel_ema_mps2_ = 0.0;

    std::uint64_t last_monte_carlo_us_ = 0;
    core::DispersionResult last_dispersion_;

    // Floating-window rate limiting: floating_window_hz normally, up to
    // 5 Hz during COUNTDOWN, immediate on mission-state change.
    std::uint64_t last_display_us_ = 0;
    MissionStateId last_display_state_ = MissionStateId::kIdle;
    bool have_display_state_ = false;
};

}  // namespace droppoint::app
