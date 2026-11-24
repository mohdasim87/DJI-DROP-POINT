#include "app/mission_state.h"
#include "app/release_manager.h"
#include "gating.h"
#include "hal/i_audio.h"
#include "hal/i_release.h"
#include "math_constants.h"
#include "test_framework.h"

using namespace droppoint::core;
namespace dp = droppoint;

namespace {
gating::Inputs BaselineGoodInputs() {
    gating::Inputs in;
    in.target_latched = true;
    in.target_age_s = 10.0;
    in.target_confirmed = true;
    in.depression_angle_rad = 60.0 * kPi / 180.0;
    in.crosswind_mps = 2.0;
    in.agl_m = 50.0;
    in.gnss_satellites = 18;
    in.rtk_status = gating::RtkStatus::kFix;
    in.telemetry_age_s = 0.05;
    in.acceleration_mps2 = 0.1;
    in.terrain_elevation_available = true;
    in.inverse_solver_converged = true;
    in.predicted_impact_divergence_m = 1.0;
    in.gust_sigma_mps = 0.5;
    in.payload_calibrated = true;
    in.target_elevation_source = ElevationSource::kLrfCorrected;
    in.predicted_impact_in_no_drop_polygon = false;
    return in;
}
}  // namespace

TEST_CASE("Gating: all-good inputs trip no hard gates") {
    const auto eval = gating::Evaluate(BaselineGoodInputs(), gating::Thresholds{});
    REQUIRE(!eval.any_hard_tripped);
}

TEST_CASE("Gating: no target latched is a hard gate") {
    auto in = BaselineGoodInputs();
    in.target_latched = false;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
    REQUIRE(!eval.primary_hold_reason.empty());
}

TEST_CASE("Gating: stale target is a hard gate") {
    auto in = BaselineGoodInputs();
    in.target_age_s = 200.0;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: crosswind above limit is a hard gate") {
    auto in = BaselineGoodInputs();
    gating::Thresholds th;
    th.crosswind_limit_mps = 5.0;
    in.crosswind_mps = 6.0;
    const auto eval = gating::Evaluate(in, th);
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: AGL below minimum is a hard gate") {
    auto in = BaselineGoodInputs();
    gating::Thresholds th;
    th.min_release_agl_m = 20.0;
    in.agl_m = 10.0;
    const auto eval = gating::Evaluate(in, th);
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: low GNSS satellite count is a hard gate") {
    auto in = BaselineGoodInputs();
    in.gnss_satellites = 5;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: RTK required but not FIX is a hard gate") {
    auto in = BaselineGoodInputs();
    gating::Thresholds th;
    th.rtk_required = true;
    in.rtk_status = gating::RtkStatus::kFloat;
    const auto eval = gating::Evaluate(in, th);
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: stale telemetry is a hard gate") {
    auto in = BaselineGoodInputs();
    in.telemetry_age_s = 1.0;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: excessive acceleration is a hard gate") {
    auto in = BaselineGoodInputs();
    in.acceleration_mps2 = 2.0;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: no terrain elevation is a hard gate") {
    auto in = BaselineGoodInputs();
    in.terrain_elevation_available = false;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: inverse solver failure is a hard gate") {
    auto in = BaselineGoodInputs();
    in.inverse_solver_converged = false;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: solution divergence beyond limit is a hard gate") {
    auto in = BaselineGoodInputs();
    in.predicted_impact_divergence_m = 30.0;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: no-drop polygon violation is a hard gate") {
    auto in = BaselineGoodInputs();
    in.predicted_impact_in_no_drop_polygon = true;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: shallow depression angle warns but does not refuse") {
    auto in = BaselineGoodInputs();
    in.depression_angle_rad = 20.0 * kPi / 180.0;  // between refuse(15) and warn(30)
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(!eval.any_hard_tripped);
    REQUIRE(eval.any_soft_tripped);
}

TEST_CASE("Gating: very shallow depression angle refuses (hard)") {
    auto in = BaselineGoodInputs();
    in.depression_angle_rad = 10.0 * kPi / 180.0;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: soft gates never block arming alone") {
    auto in = BaselineGoodInputs();
    in.gust_sigma_mps = 5.0;
    in.agl_m = 95.0;
    in.payload_calibrated = false;
    in.target_elevation_source = ElevationSource::kRawDem;
    in.rtk_status = gating::RtkStatus::kFloat;  // rtk not required in default thresholds
    in.wind_model_calibrated = false;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(!eval.any_hard_tripped);
    REQUIRE(eval.any_soft_tripped);
}

TEST_CASE("Gating: missing wind estimate is a hard gate") {
    auto in = BaselineGoodInputs();
    in.wind_valid = false;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(eval.any_hard_tripped);
    REQUIRE(eval.primary_hold_reason.find("WIND") != std::string::npos);
}

TEST_CASE("Gating: AGL above the payload maximum is a hard gate") {
    auto in = BaselineGoodInputs();
    gating::Thresholds th;
    th.max_release_agl_m = 40.0;
    in.agl_m = 50.0;
    const auto eval = gating::Evaluate(in, th);
    REQUIRE(eval.any_hard_tripped);
}

TEST_CASE("Gating: uncalibrated wind model is a soft gate") {
    auto in = BaselineGoodInputs();
    in.wind_model_calibrated = false;
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(!eval.any_hard_tripped);
    REQUIRE(eval.any_soft_tripped);
}

TEST_CASE("Gating: target elevation source messages vary by source") {
    auto find_msg = [](const gating::Evaluation& e, gating::GateId id) -> std::string {
        for (const auto& r : e.results) {
            if (r.id == id) return r.message;
        }
        return "";
    };
    {
        auto in = BaselineGoodInputs();
        in.target_elevation_source = ElevationSource::kHomeTiedDem;
        const auto eval = gating::Evaluate(in, gating::Thresholds{});
        REQUIRE(find_msg(eval, gating::GateId::kTargetElevationRawDem) ==
                "TGT ELEV DEM+HOME TIE - NOT LASER RANGED");
    }
    {
        auto in = BaselineGoodInputs();
        in.target_elevation_source = ElevationSource::kFlatAtHome;
        const auto eval = gating::Evaluate(in, gating::Thresholds{});
        REQUIRE(find_msg(eval, gating::GateId::kTargetElevationRawDem) ==
                "FLAT TERRAIN ASSUMED - TGT ELEV = TAKEOFF ELEV");
    }
    {
        auto in = BaselineGoodInputs();
        in.target_elevation_source = ElevationSource::kRawDem;
        const auto eval = gating::Evaluate(in, gating::Thresholds{});
        REQUIRE(find_msg(eval, gating::GateId::kTargetElevationRawDem) ==
                "TGT ELEV FROM DEM ONLY - RANGE THE TARGET FIRST");
    }
}

namespace {

struct FakeRelease : dp::hal::IRelease {
    dp::hal::ReleaseState state = dp::hal::ReleaseState::kSafe;
    int fire_count = 0;
    void Fire() override {
        if (state != dp::hal::ReleaseState::kSafe) return;
        state = dp::hal::ReleaseState::kFired;
        ++fire_count;
    }
    void SetSafe() override { state = dp::hal::ReleaseState::kSafe; }
    dp::hal::ReleaseState GetState() override { return state; }
    bool IsResponding() override { return true; }
};

struct FakeAudio : dp::hal::IAudio {
    std::vector<std::string> events;
    void Speak(const std::string& p) override { events.push_back(p); }
    void PlayTone() override { events.push_back("tone"); }
    bool IsBusy() override { return false; }
};

}  // namespace

TEST_CASE("Gating: auto mode aborts for EACH hard gate individually") {
    struct Trip {
        const char* name;
        std::function<void(gating::Inputs&, gating::Thresholds&)> apply;
    };
    const std::vector<Trip> trips = {
        {"kNoTarget", [](gating::Inputs& i, gating::Thresholds&) { i.target_latched = false; }},
        {"kTargetStale", [](gating::Inputs& i, gating::Thresholds&) { i.target_age_s = 200.0; }},
        {"kDepressionAngleTooLow",
         [](gating::Inputs& i, gating::Thresholds&) { i.depression_angle_rad = 10.0 * kPi / 180.0; }},
        {"kCrosswindExceeded",
         [](gating::Inputs& i, gating::Thresholds& t) {
             t.crosswind_limit_mps = 5.0;
             i.crosswind_mps = 6.0;
         }},
        {"kAglBelowMinimum",
         [](gating::Inputs& i, gating::Thresholds& t) {
             t.min_release_agl_m = 60.0;
             i.agl_m = 50.0;
         }},
        {"kAglAboveMaximum",
         [](gating::Inputs&, gating::Thresholds& t) { t.max_release_agl_m = 40.0; }},
        {"kGnssSatellitesLow", [](gating::Inputs& i, gating::Thresholds&) { i.gnss_satellites = 5; }},
        {"kRtkNotFix",
         [](gating::Inputs& i, gating::Thresholds& t) {
             t.rtk_required = true;
             i.rtk_status = gating::RtkStatus::kFloat;
         }},
        {"kTelemetryStale", [](gating::Inputs& i, gating::Thresholds&) { i.telemetry_age_s = 1.0; }},
        {"kAccelerationExcessive",
         [](gating::Inputs& i, gating::Thresholds&) { i.acceleration_mps2 = 2.0; }},
        {"kNoTerrainElevation",
         [](gating::Inputs& i, gating::Thresholds&) { i.terrain_elevation_available = false; }},
        {"kInverseSolverFailed",
         [](gating::Inputs& i, gating::Thresholds&) { i.inverse_solver_converged = false; }},
        {"kSolutionDivergence",
         [](gating::Inputs& i, gating::Thresholds&) { i.predicted_impact_divergence_m = 30.0; }},
        {"kNoDropPolygonViolation",
         [](gating::Inputs& i, gating::Thresholds&) { i.predicted_impact_in_no_drop_polygon = true; }},
        {"kNoWindEstimate", [](gating::Inputs& i, gating::Thresholds&) { i.wind_valid = false; }},
    };

    for (const auto& trip : trips) {
        gating::Inputs in = BaselineGoodInputs();
        gating::Thresholds th;
        trip.apply(in, th);
        const auto eval = gating::Evaluate(in, th);
        REQUIRE(eval.any_hard_tripped);

        // Drive a ReleaseManager with these gates while ARMED: it must
        // abort, safe the mechanism, and never fire.
        FakeRelease release;
        FakeAudio audio;
        dp::app::MissionState mission;
        dp::app::ReleaseManager rm(release, audio, mission);
        Target target;
        target.valid = true;
        mission.OnTargetDesignated(target);
        mission.OnConfirmTarget(0);
        REQUIRE(mission.TrySetMode(dp::app::ReleaseMode::kAuto, true, nullptr));
        REQUIRE(mission.TryArm(0, true, nullptr));

        dp::app::ReleaseManagerInputs rmin;
        rmin.now_us = 1'000'000;
        rmin.gates_pass = !eval.any_hard_tripped;
        rmin.trip_reason = eval.primary_hold_reason;
        rmin.target_confirmed_age_s = 10.0;
        rmin.run_in_mode = true;
        rmin.release_window_open = true;
        rmin.time_to_release_s = 30.0;
        rm.Update(rmin);
        REQUIRE(release.fire_count == 0);
        REQUIRE(release.state == dp::hal::ReleaseState::kSafe);
        REQUIRE(mission.Id() == dp::app::MissionStateId::kConfirmed);
    }
}
