// app/guidance_loop.h
//
// The 10 Hz guidance loop (spec section 6.4/6.7/9): pulls telemetry and
// gimbal attitude, runs the forward/inverse ballistics solve and gating,
// drives the floating window/audio/liveview HAL interfaces, and feeds the
// release manager. This is the top-level orchestration; all the actual
// math lives in core/.
#pragma once

#include <memory>
#include <optional>

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

namespace droppoint::app {

struct GuidanceLoopConfig {
    core::gating::Thresholds gate_thresholds;
    core::PayloadProfile payload;
    core::geodesy::EnuOrigin origin;
    core::terrain::DemSampleFn dem_fn;
    double wind_alpha = 0.14;
    bool rtk_required = false;
};

class GuidanceLoop {
public:
    GuidanceLoop(hal::ITelemetry& telemetry, hal::IGimbal& gimbal, hal::IRangefinder& rangefinder,
                 hal::IDisplay& display, hal::IAudio& audio, hal::ILiveview& liveview,
                 hal::IRelease& release, std::vector<hal::ITargetSource*> target_sources,
                 GuidanceLoopConfig config);

    // Runs one cycle. `manual_release_pressed`/`hardware_abort_pressed`/etc.
    // come from polled RC button events or widget presses, resolved by
    // main's input dispatch.
    struct CycleInputs {
        std::uint64_t now_us = 0;
        bool set_target_pressed = false;
        bool confirm_target_pressed = false;
        bool clear_target_pressed = false;
        bool arm_pressed = false;
        bool manual_release_pressed = false;
        bool hardware_abort_pressed = false;
        double acceleration_mps2 = 0.0;  // supplied by main from IMU/telemetry derivative
    };
    core::Solution Tick(const CycleInputs& in);

    MissionState& Mission() { return mission_; }

private:
    void UpdateDatumBiasFromRangefinder(std::uint64_t now_us);
    core::gating::Inputs BuildGateInputs(const core::AircraftState& aircraft, const core::Solution& solution,
                                          double acceleration_mps2) const;
    std::string BuildFloatingWindowText(const core::Solution& solution, const core::gating::Evaluation& gates) const;

    hal::ITelemetry& telemetry_;
    hal::IGimbal& gimbal_;
    hal::IRangefinder& rangefinder_;
    hal::IDisplay& display_;
    hal::IAudio& audio_;
    hal::ILiveview& liveview_;
    std::vector<hal::ITargetSource*> target_sources_;
    GuidanceLoopConfig config_;

    MissionState mission_;
    ReleaseManager release_manager_;
    core::terrain::DatumBias datum_bias_;
};

}  // namespace droppoint::app
