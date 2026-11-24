// app/release_manager.h
//
// Owns the actual IRelease and IAudio handles and is the single place that
// is allowed to call Fire(). Implements spec section 8: shared countdown
// cues for both modes, manual mode fires on operator RELEASE press, auto
// mode fires itself leading by the measured mechanism delay and only inside
// the release corridor (release_window_open), and the hardware abort and
// DISARM buttons override everything (spec 8.3/8.4).
//
// Fail-safe (spec 8.4): SetSafe() is called on construction and on every
// cycle where the mission is not in ARMED or COUNTDOWN — except during the
// post-fire RELEASED hold, where the mechanism is deliberately left in the
// fired state for release_hold_s so a servo/hook has time to open before
// being driven safe.
#pragma once

#include <cstdint>
#include <string>

#include "hal/i_audio.h"
#include "hal/i_release.h"
#include "mission_state.h"

namespace droppoint::app {

struct ReleaseManagerInputs {
    std::uint64_t now_us = 0;
    double time_to_release_s = 0.0;  // from core::Solution, may be negative once past the window
    double mechanism_delay_s = 0.0;  // from the active PayloadProfile
    double target_confirmed_age_s = 1e9;
    bool gates_pass = false;
    std::string trip_reason;
    bool manual_release_pressed = false;
    bool hardware_abort_pressed = false;
    bool disarm_pressed = false;
    bool no_drop_polygon_violation = false;
    bool run_in_mode = false;         // guidance loop is on a run-in (not hover)
    bool release_window_open = false; // predicted miss inside payload target_radius_m
    double release_hold_s = 1.5;      // how long to hold the fired state before SetSafe
    bool auto_release_allowed = true; // false when the release backend is operator-actuated
};

class ReleaseManager {
public:
    ReleaseManager(hal::IRelease& release, hal::IAudio& audio, MissionState& mission)
        : release_(release), audio_(audio), mission_(mission) {
        release_.SetSafe();
    }

    // Advances the countdown/audio/fire logic for one guidance-loop cycle.
    // Must be called every cycle regardless of mission state so the
    // fail-safe SetSafe() path always runs. Runs BEFORE MissionState::Tick
    // so a gate trip de-arms the mechanism the same cycle it is detected.
    void Update(const ReleaseManagerInputs& in);

    bool JustFired() const { return just_fired_; }

private:
    void EmitCueIfDue(double time_to_release_s);
    void ResetCues() {
        next_cue_index_ = 0;
        auto_armed_announced_ = false;
    }
    void Fire(std::uint64_t now_us);
    void Abort(const std::string& reason);

    hal::IRelease& release_;
    hal::IAudio& audio_;
    MissionState& mission_;
    int next_cue_index_ = 0;  // index into the T-5/-3/-2/-1 cue table
    bool just_fired_ = false;
    bool auto_armed_announced_ = false;  // "auto armed" spoken once per arm
    std::uint64_t fired_at_us_ = 0;
};

}  // namespace droppoint::app
