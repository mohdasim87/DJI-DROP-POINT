#include "math_constants.h"
#include "wind.h"
#include "test_framework.h"

using namespace droppoint::core;

TEST_CASE("Wind: power law increases with height") {
    const double alpha = wind::AlphaForSurface(wind::SurfaceType::kOpenCountry);
    REQUIRE_REL_NEAR(alpha, 0.14, 1e-9);
    const double v10 = wind::SpeedAtHeight(5.0, 50.0, 10.0, alpha);
    const double v50 = wind::SpeedAtHeight(5.0, 50.0, 50.0, alpha);
    REQUIRE(v50 > v10);
    REQUIRE_REL_NEAR(v50, 5.0, 1e-9);
}

TEST_CASE("Wind: below 0.5m returns zero") {
    REQUIRE_NEAR(wind::SpeedAtHeight(5.0, 50.0, 0.3, 0.14), 0.0, 1e-12);
}

TEST_CASE("Wind: direction convention, from 270 blows toward East") {
    const EnuVector v = wind::ToVector(5.0, 270.0 * kPi / 180.0);
    REQUIRE(v.e > 4.9);   // blowing toward east: positive E component
    REQUIRE_NEAR(v.n, 0.0, 1e-6);
}

TEST_CASE("Wind: hover estimator produces a valid sample") {
    wind::HoverEstimateInputs in;
    in.pitch_rad = 0.05;
    in.roll_rad = 0.0;
    in.yaw_rad = 0.0;
    in.mass_kg = 1.5;
    in.cda_airframe = 0.3;
    in.air_density_kg_m3 = 1.2;
    const WindSample s = wind::EstimateFromHover(in);
    REQUIRE(s.valid);
    REQUIRE(s.speed_mps > 0.0);
}

TEST_CASE("Wind: hover estimator direction, north heading pitch-down means wind from north") {
    // ENU-math yaw: north heading = pi/2. A lean into the wind (nose dips
    // toward the wind source) reads as pitch < 0 with pitch + = nose up.
    wind::HoverEstimateInputs in;
    in.pitch_rad = -0.05;
    in.roll_rad = 0.0;
    in.yaw_rad = kPi / 2.0;  // heading north
    in.mass_kg = 1.5;
    in.cda_airframe = 0.3;
    in.air_density_kg_m3 = 1.2;
    const WindSample s = wind::EstimateFromHover(in);
    REQUIRE(s.valid);
    REQUIRE_NEAR(s.from_bearing_rad, 0.0, 1e-6);  // from north
}

TEST_CASE("Wind: hover estimator direction, right wing down means wind from east") {
    wind::HoverEstimateInputs in;
    in.pitch_rad = 0.0;
    in.roll_rad = 0.05;
    in.yaw_rad = kPi / 2.0;  // heading north; right wing down => leans east
    in.mass_kg = 1.5;
    in.cda_airframe = 0.3;
    in.air_density_kg_m3 = 1.2;
    const WindSample s = wind::EstimateFromHover(in);
    REQUIRE(s.valid);
    REQUIRE_NEAR(s.from_bearing_rad, kPi / 2.0, 1e-6);  // from east
}

TEST_CASE("Wind: hover estimator direction, east heading pitch-down means wind from east") {
    wind::HoverEstimateInputs in;
    in.pitch_rad = -0.05;
    in.roll_rad = 0.0;
    in.yaw_rad = 0.0;  // heading east (ENU math angle 0)
    in.mass_kg = 1.5;
    in.cda_airframe = 0.3;
    in.air_density_kg_m3 = 1.2;
    const WindSample s = wind::EstimateFromHover(in);
    REQUIRE(s.valid);
    REQUIRE_NEAR(s.from_bearing_rad, kPi / 2.0, 1e-6);  // from east
}

TEST_CASE("Wind: hover estimator speed matches the drag-balance formula") {
    const double theta = 0.08;
    wind::HoverEstimateInputs in;
    in.pitch_rad = -theta;
    in.roll_rad = 0.0;
    in.yaw_rad = kPi / 2.0;
    in.mass_kg = 6.3;
    in.cda_airframe = 0.45;
    in.air_density_kg_m3 = 1.2;
    const WindSample s = wind::EstimateFromHover(in);
    REQUIRE(s.valid);
    const double expected =
        std::sqrt(2.0 * in.mass_kg * 9.80665 * std::tan(theta) / (in.air_density_kg_m3 * in.cda_airframe));
    REQUIRE_REL_NEAR(s.speed_mps, expected, 1e-9);
}

TEST_CASE("Wind: disagreement flags large speed differences") {
    WindSample telemetry;
    telemetry.valid = true;
    telemetry.speed_mps = 10.0;
    telemetry.from_bearing_rad = 0.0;
    WindSample hover;
    hover.valid = true;
    hover.speed_mps = 2.0;
    hover.from_bearing_rad = 0.0;
    const auto cmp = wind::CompareWindSources(telemetry, hover);
    REQUIRE(cmp.disagrees);
    REQUIRE_NEAR(cmp.recommended.speed_mps, 10.0, 1e-9);
}
