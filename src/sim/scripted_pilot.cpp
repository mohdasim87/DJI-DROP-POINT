#include "scripted_pilot.h"

#include <algorithm>
#include <cmath>

#include "core/geodesy.h"

namespace droppoint::sim {

namespace {
constexpr double kWindAcquireS = 8.0;       // hover before designating (fills the estimator window)
constexpr double kConfirmRetryS = 2.0;
constexpr double kRunInDistanceM = 65.0;    // start point this far up-track of the aim point
constexpr double kPositionGain = 0.6;       // m/s per metre of position error
constexpr double kApproachMaxSpeedMps = 3.0;
constexpr double kRepositionMaxSpeedMps = 4.5;
constexpr double kWorldAccelMps2 = 0.35;    // slew limit margin under the world's 0.4
constexpr double kLateralGain = 1.0;
constexpr double kLateralDamping = 1.0;   // velocity feedback: plant is slew-limited, needs damping
constexpr double kLateralMaxSpeedMps = 2.0;
constexpr double kSteadyBeforeArmS = 1.5;
constexpr double kArmMissLimitM = 25.0;     // match the kSolutionDivergence hard gate
}  // namespace

ScriptedPilot::ScriptedPilot(std::shared_ptr<SimWorld> world,
                             SimCrosshairTargetSource* target_source, Plan plan,
                             double mechanism_delay_s)
    : world_(std::move(world)), target_source_(target_source), plan_(plan),
      mechanism_delay_s_(mechanism_delay_s),
      request_auto_(plan == Plan::kRunInAuto) {
    phase_entered_s_ = world_->NowS();
}

core::EnuVector ScriptedPilot::VelocityToward(const core::EnuPoint& from, const core::EnuPoint& to,
                                            double max_speed) const {
    const double de = to.e - from.e;
    const double dn = to.n - from.n;
    const double d = std::sqrt(de * de + dn * dn);
    core::EnuVector v{0, 0, 0};
    if (d < 1e-6) return v;
    // Deceleration-aware cap: the world's velocity slew (0.4 m/s^2, with
    // margin) cannot shed more than sqrt(2*a*d) in the remaining distance,
    // so approaching any faster overshoots the target point.
    const double v_stop = std::sqrt(2.0 * kWorldAccelMps2 * d);
    const double speed = std::min({max_speed, kPositionGain * d, v_stop});
    v.e = speed * de / d;
    v.n = speed * dn / d;
    return v;
}

void ScriptedPilot::EnterHold() {
    phase_ = Phase::kHold;
    phase_entered_s_ = world_->NowS();
}

app::GuidanceLoop::CycleInputs ScriptedPilot::Tick(const core::Solution& solution,
                                                   const app::MissionState& mission) {
    app::GuidanceLoop::CycleInputs in;
    in.now_us = world_->NowUs();

    if (request_auto_ && mission.Mode() != app::ReleaseMode::kAuto) {
        in.requested_mode = app::ReleaseMode::kAuto;
    }

    const core::EnuPoint ac = world_->Position();
    const bool aborted = !mission.LastAbortReason().empty();
    if (aborted && phase_ != Phase::kHold && phase_ != Phase::kDone) {
        EnterHold();
    }
    if (mission.Id() == app::MissionStateId::kReleased) {
        phase_ = Phase::kDone;
    }
    if (mission.Id() == app::MissionStateId::kIdle &&
        (phase_ == Phase::kDone || phase_ == Phase::kHold)) {
        world_->SetCommandedVelocity({0, 0, 0});
        return in;
    }

    const double in_phase_s = world_->NowS() - phase_entered_s_;
    core::EnuVector cmd{0, 0, 0};

    switch (phase_) {
        case Phase::kAcquire:
            // Hold the initial hover so the wind estimator fills its window.
            if (world_->NowS() >= kWindAcquireS) {
                phase_ = Phase::kDesignate;
                phase_entered_s_ = world_->NowS();
            }
            break;

        case Phase::kDesignate:
            in.set_target_pressed = true;
            phase_ = Phase::kConfirm;
            phase_entered_s_ = world_->NowS();
            break;

        case Phase::kConfirm:
            in.confirm_target_pressed = true;
            phase_ = Phase::kWaitConfirm;
            phase_entered_s_ = world_->NowS();
            break;

        case Phase::kWaitConfirm:
            if (mission.Id() == app::MissionStateId::kConfirmed) {
                // The intended aimpoint is the true (pre-offset) raycast
                // hit; identical to the latched target in every scenario
                // except mis-designated-target.
                if (target_source_ && target_source_->LastTrueHitEnu().has_value()) {
                    aim_enu_ = *target_source_->LastTrueHitEnu();
                } else {
                    aim_enu_ = mission.CurrentTarget().enu;
                }
                have_aim_ = true;
                phase_ = plan_ == Plan::kHoverManual ? Phase::kApproach : Phase::kReposition;
                phase_entered_s_ = world_->NowS();
            } else if (in_phase_s > kConfirmRetryS) {
                if (designate_retries_ < 5) {
                    ++designate_retries_;
                    phase_ = Phase::kDesignate;
                    phase_entered_s_ = world_->NowS();
                } else {
                    EnterHold();
                }
            }
            break;

        case Phase::kApproach: {
            if (!mission.CurrentTarget().valid) break;
            // Slide the hover point so the impact-if-released-now lands on
            // the target: desired aircraft position = target - current drift.
            if (solution.forward_valid) {
                const core::EnuPoint desired{
                    ac.e + (mission.CurrentTarget().enu.e - solution.impact_if_released_now.e),
                    ac.n + (mission.CurrentTarget().enu.n - solution.impact_if_released_now.n),
                    ac.u};
                cmd = VelocityToward(ac, desired, kApproachMaxSpeedMps);
            }
            if (mission.Id() == app::MissionStateId::kConfirmed) {
                in.arm_pressed = true;  // retried every cycle; gates decide when it lands
            } else if (mission.Id() == app::MissionStateId::kArmed && solution.hover_mode &&
                       solution.release_window_open && !hold_fire_) {
                // Press only once actually on station: in run_in_mode the
                // corridor "opens" the moment the solver converges — which
                // can be tens of seconds of flight before the release
                // point — and a manual release ignores time_to_release.
                in.manual_release_pressed = true;
            }
            break;
        }

        case Phase::kReposition: {
            if (!have_aim_) break;
            const double hdg = world_->Config().run_in_heading_deg * core::kPi / 180.0;
            const double dir_e = std::sin(hdg), dir_n = std::cos(hdg);
            const double perp_e = -dir_n, perp_n = dir_e;
            // Shift the start point upwind by the LATERAL component of the
            // current drift only, so the run line is already offset for the
            // crosswind without changing the along-track run distance.
            double drift_cross = 0.0;
            if (solution.forward_valid) {
                // Correct for the aircraft's own lateral carry: the measured
                // impact point includes current_velocity * fall_time, so the
                // pure payload-drift term excludes it.
                const double v_perp =
                    world_->Velocity().e * perp_e + world_->Velocity().n * perp_n;
                drift_cross = (solution.impact_if_released_now.e - ac.e) * perp_e +
                              (solution.impact_if_released_now.n - ac.n) * perp_n -
                              v_perp * solution.ballistics.fall_time_s;
            }
            run_in_start_ = core::EnuPoint{aim_enu_.e - dir_e * kRunInDistanceM - perp_e * drift_cross,
                                           aim_enu_.n - dir_n * kRunInDistanceM - perp_n * drift_cross,
                                           ac.u};
            cmd = VelocityToward(ac, run_in_start_, kRepositionMaxSpeedMps);
            const double dist =
                core::geodesy::HorizontalRange(ac, run_in_start_);
            if (dist < 4.0) {
                phase_ = Phase::kRunIn;
                phase_entered_s_ = world_->NowS();
            }
            break;
        }

        case Phase::kRunIn: {
            if (!have_aim_) break;
            const double hdg = world_->Config().run_in_heading_deg * core::kPi / 180.0;
            const double dir_e = std::sin(hdg), dir_n = std::cos(hdg);
            const double perp_e = -dir_n, perp_n = dir_e;  // 90 deg left of track
            // Desired lateral coordinate relative to the aim point: the
            // negative of the payload drift, so the impact lands on aim.
            double lateral_err = 0.0;
            double v_perp = 0.0;
            if (solution.forward_valid) {
                // Null the payload drift, not the composite measurement:
                // impact_rel = drift + v_lateral * fall_time, and chasing
                // the raw value makes the controller fight its own output.
                v_perp = world_->Velocity().e * perp_e + world_->Velocity().n * perp_n;
                const double drift_rel = (solution.impact_if_released_now.e - ac.e) * perp_e +
                                         (solution.impact_if_released_now.n - ac.n) * perp_n -
                                         v_perp * solution.ballistics.fall_time_s;
                const double ac_rel = (ac.e - aim_enu_.e) * perp_e + (ac.n - aim_enu_.n) * perp_n;
                lateral_err = -drift_rel - ac_rel;
            }
            // PD on the lateral offset: the correction above removes the
            // implicit damping the carry term provided, and the slew-
            // limited plant oscillates without explicit velocity damping.
            const double lateral_v =
                std::max(-kLateralMaxSpeedMps,
                         std::min(kLateralMaxSpeedMps,
                                  kLateralGain * lateral_err - kLateralDamping * v_perp));
            const double fwd = world_->Config().run_in_speed_mps;
            cmd.e = dir_e * fwd + perp_e * lateral_v;
            cmd.n = dir_n * fwd + perp_n * lateral_v;

            // Arm only once genuinely steady: on speed along track, in
            // phase a moment, and the solution not diverging. Arming while
            // still spooling up produced transient misses that tripped the
            // kSolutionDivergence hard gate and aborted the run.
            const double fwd_speed = world_->Velocity().e * dir_e + world_->Velocity().n * dir_n;
            const bool steady = in_phase_s > kSteadyBeforeArmS &&
                                fwd_speed >= 0.8 * world_->Config().run_in_speed_mps &&
                                solution.has_solution &&
                                solution.miss_distance_m < kArmMissLimitM;
            if (mission.Id() == app::MissionStateId::kConfirmed && steady) {
                in.arm_pressed = true;
            } else if (mission.Id() == app::MissionStateId::kCountdown &&
                       mission.Mode() == app::ReleaseMode::kManual &&
                       solution.release_window_open &&
                       solution.time_to_release_s <= mechanism_delay_s_ && !hold_fire_) {
                // Operator-actuated backend: AUTO was rejected, so the pilot
                // presses the kit's release at the same instant the auto
                // rule would have fired — the corridor alone opens tens of
                // seconds before the release point.
                in.manual_release_pressed = true;
            }
            break;
        }

        case Phase::kHold:
        case Phase::kDone:
            break;
    }

    world_->SetCommandedVelocity(cmd);
    return in;
}

}  // namespace droppoint::sim
