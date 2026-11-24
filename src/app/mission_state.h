// app/mission_state.h
//
// The shared release state machine, spec section 8.1:
//   IDLE -> TARGET_LATCHED -> CONFIRMED -> ARMED -> COUNTDOWN -> RELEASED -> IDLE
//                                            ^          |
//                                            +-- ABORT --+
//
// Both manual and auto release modes share this exact state machine; only
// the release trigger differs (release_manager.h). This class contains no
// I/O and no HAL dependency, only core:: types, so its transition logic is
// unit-testable without a simulator.
#pragma once

#include <cstdint>
#include <string>

#include "core/solution.h"

namespace droppoint::app {

enum class MissionStateId { kIdle, kTargetLatched, kConfirmed, kArmed, kCountdown, kReleased };

enum class ReleaseMode { kManual, kAuto };

inline constexpr double kArmExpirySeconds = 60.0;

class MissionState {
public:
    MissionStateId Id() const { return id_; }
    const core::Target& CurrentTarget() const { return target_; }
    ReleaseMode Mode() const { return mode_; }

    // Mode changes are rejected while ARMED/COUNTDOWN/RELEASED (the operator
    // must abort/disarm first) and AUTO is rejected outright when the
    // release backend is operator-actuated — an auto drop is meaningless if
    // the software cannot actuate the kit.
    bool TrySetMode(ReleaseMode mode, bool auto_allowed, std::string* reject_reason);

    // Source-agnostic: app/ calls this with whatever ITargetSource produced,
    // per spec section 3's priority order.
    void OnTargetDesignated(const core::Target& target);
    void OnClearTarget();
    void OnConfirmTarget(std::uint64_t now_us);

    // gates_pass: result of core::gating::Evaluate(...).any_hard_tripped ==
    // false, evaluated by the caller every cycle. Arming requires a latched,
    // confirmed target and all hard gates passing (spec 8.1).
    bool TryArm(std::uint64_t now_us, bool gates_pass, std::string* reject_reason);

    // Called every guidance-loop cycle; expires arming after 60 s idle
    // (spec 8.3). Hard-gate aborts while armed/counting down are owned by
    // ReleaseManager, which also commands SetSafe — a gate trip must never
    // leave the mechanism in a non-safe state for even one cycle.
    void Tick(std::uint64_t now_us);

    void EnterCountdown();

    // COUNTDOWN -> ARMED: the run-in geometry moved back outside the
    // countdown window, or a missed manual pass was reset. Resets the 60 s
    // arm-idle timer so a long approach does not expire mid-attempt.
    void ReturnToArmed(std::uint64_t now_us);
    void OnAbort(const std::string& reason);
    void OnReleased(std::uint64_t now_us);
    // Every drop requires a fresh arm: returns to IDLE and disarms.
    void ReturnToIdleAfterRelease();

    const std::string& LastAbortReason() const { return last_abort_reason_; }

private:
    MissionStateId id_ = MissionStateId::kIdle;
    ReleaseMode mode_ = ReleaseMode::kManual;
    core::Target target_;
    std::uint64_t armed_at_us_ = 0;
    std::uint64_t last_activity_us_ = 0;
    std::string last_abort_reason_;
};

}  // namespace droppoint::app
