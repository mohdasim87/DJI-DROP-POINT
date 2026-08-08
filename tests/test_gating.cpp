#include "gating.h"
#include "math_constants.h"
#include "test_framework.h"

using namespace droppoint::core;

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
    const auto eval = gating::Evaluate(in, gating::Thresholds{});
    REQUIRE(!eval.any_hard_tripped);
    REQUIRE(eval.any_soft_tripped);
}
