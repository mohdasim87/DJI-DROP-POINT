// app/external_operator_release.h
//
// Release backend for third-party drop kits (e.g. CZI TH2/TH4) that the
// OPERATOR triggers in DJI Pilot: DropPoint cannot actuate the mechanism
// itself, it can only compute and announce the release instant. Fire()
// therefore performs no I/O — it records that the release cue was issued
// (single-shot, mirroring the hardware contract so fail-safe logic is
// unchanged); the countdown/floating-window text tells the operator to
// press release on the kit instead of expecting this system to do it.
#pragma once

#include "hal/i_release.h"

namespace droppoint::app {

class ExternalOperatorRelease : public hal::IRelease {
public:
    ExternalOperatorRelease() { SetSafe(); }

    // Single-shot bookkeeping only: kSafe -> kFired, increments the fire
    // counter, performs no I/O (the operator actuates the kit).
    void Fire() override {
        if (state_ != hal::ReleaseState::kSafe) return;
        state_ = hal::ReleaseState::kFired;
        ++fire_count_;
    }

    void SetSafe() override { state_ = hal::ReleaseState::kSafe; }
    hal::ReleaseState GetState() override { return state_; }
    bool IsResponding() override { return true; }
    bool IsOperatorActuated() const override { return true; }

    int FireCount() const { return fire_count_; }

private:
    hal::ReleaseState state_ = hal::ReleaseState::kSafe;
    int fire_count_ = 0;
};

}  // namespace droppoint::app
