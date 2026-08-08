#include "release_manager.h"

#include <array>

namespace droppoint::app {

namespace {
// (threshold_s, phrase). Checked in order; the first not-yet-emitted cue
// whose threshold has been crossed fires. T-0 is issued early by the
// mechanism delay so separation happens at the computed release point
// (spec 9), which callers achieve by using mechanism_delay_s as the fire
// threshold below rather than literal T-0.0.
constexpr std::array<double, 4> kCueThresholds = {5.0, 3.0, 2.0, 1.0};
constexpr std::array<const char*, 4> kCuePhrases = {"tone", "three", "two", "one"};
}  // namespace

void ReleaseManager::Update(const ReleaseManagerInputs& in) {
    just_fired_ = false;

    // Hardware abort overrides everything, at any point, including
    // mid-countdown (spec 8.3).
    if (in.hardware_abort_pressed) {
        release_.SetSafe();
        mission_.OnAbort("hardware abort");
        audio_.Speak("abort");
        ResetCues();
        auto_armed_state_ = 0;
        return;
    }

    const MissionStateId id = mission_.Id();

    // Fail-safe: anything other than ARMED/COUNTDOWN keeps the mechanism
    // safe, unconditionally, every cycle.
    if (id != MissionStateId::kArmed && id != MissionStateId::kCountdown) {
        release_.SetSafe();
        ResetCues();
        auto_armed_state_ = 0;
        return;
    }

    // Re-evaluate every hard gate at loop rate throughout arming/countdown
    // (spec 8.3). A trip aborts, announces, and disarms.
    if (!in.gates_pass || in.no_drop_polygon_violation) {
        release_.SetSafe();
        audio_.Speak("abort");
        mission_.OnAbort(in.no_drop_polygon_violation ? "predicted impact in no-drop polygon" : in.trip_reason);
        ResetCues();
        auto_armed_state_ = 0;
        return;
    }

    // Auto mode can never fire on a stale or unconfirmed target.
    if (mission_.Mode() == ReleaseMode::kAuto && in.target_confirmed_age_s > 120.0) {
        release_.SetSafe();
        audio_.Speak("abort");
        mission_.OnAbort("target confirmation stale (>120s)");
        ResetCues();
        auto_armed_state_ = 0;
        return;
    }

    if (id == MissionStateId::kArmed) {
        if (mission_.Mode() == ReleaseMode::kAuto && auto_armed_state_ == 0) {
            audio_.Speak("auto armed");
            auto_armed_state_ = 1;
        }
        // Enter the audible countdown once inside the T-5 window.
        if (in.time_to_release_s <= kCueThresholds[0]) {
            mission_.EnterCountdown();
        } else {
            return;
        }
    }

    // COUNTDOWN: emit cues, then fire at the right instant for each mode.
    EmitCueIfDue(in.time_to_release_s);

    const bool auto_fire_time_reached =
        mission_.Mode() == ReleaseMode::kAuto && in.time_to_release_s <= in.mechanism_delay_s;
    const bool manual_fire_requested = mission_.Mode() == ReleaseMode::kManual && in.manual_release_pressed;

    if (auto_fire_time_reached || manual_fire_requested) {
        audio_.Speak("drop");
        release_.Fire();
        just_fired_ = true;
        mission_.OnReleased(in.now_us);
        ResetCues();
        auto_armed_state_ = 0;
    }
}

void ReleaseManager::EmitCueIfDue(double time_to_release_s) {
    while (next_cue_index_ < static_cast<int>(kCueThresholds.size()) &&
           time_to_release_s <= kCueThresholds[next_cue_index_]) {
        if (next_cue_index_ == 0) {
            audio_.PlayTone();
        } else {
            audio_.Speak(kCuePhrases[next_cue_index_]);
        }
        ++next_cue_index_;
    }
}

}  // namespace droppoint::app
