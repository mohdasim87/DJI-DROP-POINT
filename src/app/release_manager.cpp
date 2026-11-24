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
constexpr double kCountdownEnterS = 5.0;   // enter COUNTDOWN inside this window
constexpr double kCountdownExitS = 6.0;    // leave COUNTDOWN if the estimate jumps above this
constexpr double kAutoTargetMaxAgeS = 120.0;
}  // namespace

void ReleaseManager::Fire(std::uint64_t now_us) {
    audio_.Speak("drop");
    release_.Fire();
    just_fired_ = true;
    fired_at_us_ = now_us;
    mission_.OnReleased(now_us);
    ResetCues();
}

void ReleaseManager::Abort(const std::string& reason) {
    release_.SetSafe();
    audio_.Speak("abort");
    mission_.OnAbort(reason);
    ResetCues();
}

void ReleaseManager::Update(const ReleaseManagerInputs& in) {
    just_fired_ = false;

    // 1. Hardware abort overrides everything, at any point, including
    //    mid-countdown and mid-hold (spec 8.3).
    if (in.hardware_abort_pressed) {
        release_.SetSafe();
        const MissionStateId id = mission_.Id();
        if (id == MissionStateId::kArmed || id == MissionStateId::kCountdown) {
            mission_.OnAbort("hardware abort");
            audio_.Speak("abort");
        } else if (id == MissionStateId::kReleased) {
            mission_.ReturnToIdleAfterRelease();
        }
        ResetCues();
        return;
    }

    const MissionStateId id = mission_.Id();

    // 2. RELEASED hold: keep the mechanism in the fired state for
    //    release_hold_s so the servo/hook has time to open, then safe it and
    //    return to IDLE for the next drop.
    if (id == MissionStateId::kReleased) {
        const double held_s = (in.now_us >= fired_at_us_) ? (in.now_us - fired_at_us_) / 1e6 : 0.0;
        if (held_s >= in.release_hold_s) {
            release_.SetSafe();
            mission_.ReturnToIdleAfterRelease();
        }
        return;
    }

    // 3. Fail-safe: anything other than ARMED/COUNTDOWN keeps the mechanism
    //    safe, unconditionally, every cycle.
    if (id != MissionStateId::kArmed && id != MissionStateId::kCountdown) {
        release_.SetSafe();
        ResetCues();
        return;
    }

    // 4. Operator DISARM.
    if (in.disarm_pressed) {
        release_.SetSafe();
        mission_.OnAbort("disarmed by operator");
        audio_.Speak("disarmed");
        ResetCues();
        return;
    }

    // 5. Every hard gate is re-evaluated at loop rate throughout
    //    arming/countdown (spec 8.3). A trip aborts, announces, and disarms.
    if (!in.gates_pass || in.no_drop_polygon_violation) {
        Abort(in.no_drop_polygon_violation ? "predicted impact in no-drop polygon" : in.trip_reason);
        return;
    }

    // 6. Auto-mode guards: never fire on a stale/unconfirmed target, and
    //    never when the backend cannot be actuated by this system.
    if (mission_.Mode() == ReleaseMode::kAuto) {
        if (in.target_confirmed_age_s > kAutoTargetMaxAgeS) {
            Abort("target confirmation stale (>120s)");
            return;
        }
        if (!in.auto_release_allowed) {
            Abort("auto release unavailable");
            return;
        }
    }

    // 7. ARMED: announce once in auto, honour a manual press, and enter the
    //    audible countdown once inside the T-5 window on a run-in.
    if (id == MissionStateId::kArmed) {
        if (mission_.Mode() == ReleaseMode::kAuto && !auto_armed_announced_) {
            audio_.Speak("auto armed");
            auto_armed_announced_ = true;
        }
        if (mission_.Mode() == ReleaseMode::kManual && in.manual_release_pressed) {
            Fire(in.now_us);
            return;
        }
        if (in.run_in_mode && in.time_to_release_s <= kCountdownEnterS &&
            in.time_to_release_s > -1.0) {
            mission_.EnterCountdown();
            // fall through to the countdown logic below
        } else {
            return;
        }
    }

    // 8. COUNTDOWN.
    if (!in.run_in_mode || in.time_to_release_s > kCountdownExitS) {
        mission_.ReturnToArmed(in.now_us);
        ResetCues();
        return;
    }

    EmitCueIfDue(in.time_to_release_s);

    if (mission_.Mode() == ReleaseMode::kManual) {
        if (in.manual_release_pressed) {
            Fire(in.now_us);
            return;
        }
        if (in.time_to_release_s < -2.0) {
            // Flew past the release point without a press.
            mission_.ReturnToArmed(in.now_us);
            audio_.Speak("missed");
            ResetCues();
        }
        return;
    }

    // Auto: fire leading the computed release instant by the mechanism
    // delay, and only while the predicted impact stays inside the release
    // corridor. Hover never reaches here (no run-in, no countdown).
    if (in.time_to_release_s <= in.mechanism_delay_s) {
        if (in.release_window_open) {
            Fire(in.now_us);
        } else {
            Abort("run-in outside release corridor");
        }
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
