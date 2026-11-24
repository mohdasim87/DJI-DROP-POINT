// sim/scripted_pilot.h
//
// A scripted operator/pilot that drives SimWorld + GuidanceLoop through a
// complete drop without human input (spec 12 test scaffolding):
//
//   1. Hold a hover for >= 8 s so the wind estimator fills its window.
//   2. SET TARGET, then CONFIRM.
//   3a. HOVER-MANUAL: slide the hover point so steer_distance -> 0, ARM
//       once gates pass, press RELEASE when the release window opens.
//   3b. RUN-IN-AUTO: request AUTO, reposition to a start point up-track of
//       the aim point, fly a constant-speed run-in steering to null the
//       cross-track miss, ARM once steady, and let auto fire — or press
//       RELEASE itself when the backend is operator-actuated and AUTO was
//       rejected.
//
// The pilot acts on the previous tick's Solution + MissionState, like a
// real operator reading the floating window.
#pragma once

#include <memory>
#include <optional>

#include "app/guidance_loop.h"
#include "sim_hal.h"
#include "sim_world.h"

namespace droppoint::sim {

class ScriptedPilot {
public:
    enum class Plan { kHoverManual, kRunInAuto };

    ScriptedPilot(std::shared_ptr<SimWorld> world, SimCrosshairTargetSource* target_source,
                  Plan plan, double mechanism_delay_s = 0.15);

    // Asks the loop for AUTO mode every cycle. Run-in-auto does this on its
    // own; exposed so a manual-plan run can still exercise the rejection
    // path on an operator-actuated backend.
    void RequestAutoMode() { request_auto_ = true; }

    // When set, the pilot never presses RELEASE itself (auto fire is
    // unaffected): holds the aircraft in the armed-and-ready state, used by
    // tests that need the floating-window text to render first.
    void SetHoldFire(bool hold) { hold_fire_ = hold; }

    // Computes this tick's world velocity command (applied immediately via
    // SimWorld::SetCommandedVelocity) and returns the cycle inputs to pass
    // to GuidanceLoop::Tick for the same tick.
    app::GuidanceLoop::CycleInputs Tick(const core::Solution& solution,
                                        const app::MissionState& mission);

    bool Finished() const { return phase_ == Phase::kDone; }

private:
    enum class Phase {
        kAcquire,      // holding the initial hover for the wind estimate
        kDesignate,    // SET TARGET pressed this tick
        kConfirm,      // CONFIRM pressed this tick
        kWaitConfirm,  // waiting for the mission to reach CONFIRMED
        kApproach,     // hover-manual: sliding onto the steer point
        kReposition,   // run-in-auto: transiting to the run-in start point
        kRunIn,        // run-in-auto: constant-speed run toward the release point
        kHold,         // terminal: hold position, no further inputs
        kDone,         // released
    };

    core::EnuVector VelocityToward(const core::EnuPoint& from, const core::EnuPoint& to,
                                   double max_speed) const;
    void EnterHold();

    std::shared_ptr<SimWorld> world_;
    SimCrosshairTargetSource* target_source_;
    Plan plan_;
    Phase phase_ = Phase::kAcquire;
    double mechanism_delay_s_;
    bool request_auto_ = false;
    bool hold_fire_ = false;

    double phase_entered_s_ = 0.0;
    int designate_retries_ = 0;
    core::EnuPoint aim_enu_{};      // intended drop point (un-offset truth designation)
    bool have_aim_ = false;
    core::EnuPoint run_in_start_{}; // reposition target for the run-in plan
};

}  // namespace droppoint::sim
