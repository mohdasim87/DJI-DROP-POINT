#include "mission_state.h"

namespace droppoint::app {

void MissionState::OnTargetDesignated(const core::Target& target) {
    // A fresh designation while armed/counting-down aborts first: per spec,
    // auto mode can never fire on a stale or unconfirmed target, and
    // re-designation implies exactly that.
    if (id_ == MissionStateId::kArmed || id_ == MissionStateId::kCountdown) {
        OnAbort("re-designated while armed");
    }
    target_ = target;
    target_.confirmed = false;
    id_ = MissionStateId::kTargetLatched;
}

void MissionState::OnClearTarget() {
    if (id_ == MissionStateId::kArmed || id_ == MissionStateId::kCountdown) {
        OnAbort("target cleared while armed");
    }
    target_ = core::Target{};
    id_ = MissionStateId::kIdle;
}

void MissionState::OnConfirmTarget(std::uint64_t now_us) {
    if (id_ != MissionStateId::kTargetLatched) return;
    target_.confirmed = true;
    target_.confirmed_at_us = now_us;
    id_ = MissionStateId::kConfirmed;
}

bool MissionState::TryArm(std::uint64_t now_us, bool gates_pass, std::string* reject_reason) {
    if (id_ != MissionStateId::kConfirmed) {
        if (reject_reason) *reject_reason = "no confirmed target";
        return false;
    }
    if (!gates_pass) {
        if (reject_reason) *reject_reason = "hard gate(s) failing";
        return false;
    }
    id_ = MissionStateId::kArmed;
    armed_at_us_ = now_us;
    last_activity_us_ = now_us;
    return true;
}

void MissionState::Tick(std::uint64_t now_us, bool gates_pass, const std::string& trip_reason) {
    if (id_ != MissionStateId::kArmed && id_ != MissionStateId::kCountdown) return;

    if (!gates_pass) {
        OnAbort(trip_reason.empty() ? "hard gate tripped" : trip_reason);
        return;
    }

    if (id_ == MissionStateId::kArmed) {
        const double idle_s = (now_us - last_activity_us_) / 1e6;
        if (idle_s > kArmExpirySeconds) {
            OnAbort("arm auto-expired after 60s idle");
            return;
        }
    }
}

void MissionState::EnterCountdown() {
    if (id_ != MissionStateId::kArmed) return;
    id_ = MissionStateId::kCountdown;
}

void MissionState::OnAbort(const std::string& reason) {
    last_abort_reason_ = reason;
    id_ = MissionStateId::kConfirmed;  // fall back to confirmed; must re-arm
}

void MissionState::OnReleased(std::uint64_t now_us) {
    (void)now_us;
    id_ = MissionStateId::kReleased;
}

void MissionState::ReturnToIdleAfterRelease() {
    target_ = core::Target{};
    id_ = MissionStateId::kIdle;
}

}  // namespace droppoint::app
