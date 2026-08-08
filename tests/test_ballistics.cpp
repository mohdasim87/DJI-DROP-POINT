#include "atmosphere.h"
#include "ballistics.h"
#include "math_constants.h"
#include "test_framework.h"
#include "wind.h"

using namespace droppoint::core;

namespace {

ballistics::WindAtHeightFn NoWind() {
    return [](double) { return EnuVector{0.0, 0.0, 0.0}; };
}

ballistics::CdaAtTimeFn ConstCda(double cda) {
    return [cda](double) { return cda; };
}

}  // namespace

// Regression vector 1: no drag, 0.5 kg, 50 m, 5 m/s horizontal, no wind, sea
// level -> fall 3.1933 s, range 15.966 m, 0.1% tolerance.
TEST_CASE("Regression 1: no-drag free fall with horizontal carry") {
    ballistics::ForwardInputs in;
    in.mass_kg = 0.5;
    in.cda_at_time = ConstCda(1e-9);
    in.release_height_agl_m = 50.0;
    in.initial_velocity = EnuVector{0.0, 5.0, 0.0};
    in.wind_at_height = NoWind();
    in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 15.0);

    const auto result = ballistics::IntegrateForward(in);
    REQUIRE(result.converged);
    REQUIRE_REL_NEAR(result.fall_time_s, 3.1933, 0.001);
    const double range = std::sqrt(result.impact_displacement.e * result.impact_displacement.e +
                                    result.impact_displacement.n * result.impact_displacement.n);
    REQUIRE_REL_NEAR(range, 15.966, 0.001);
}

// Regression vector 2: 0.5 kg, CdA 0.030, 50 m, GS 5.0 on heading 000, wind
// 5.0 from 270, alpha 0.14, 38 C, elev 0 -> dE 9.44 m, dN 10.88 m, total
// 14.40 m, fall 4.19 s, 2% tolerance. Wind reference height = release AGL.
TEST_CASE("Regression 2: crosswind drift with power-law profile") {
    const double alpha = wind::AlphaForSurface(wind::SurfaceType::kOpenCountry);
    const double z_ref = 50.0;
    const double wind_from_rad = 270.0 * kPi / 180.0;

    ballistics::ForwardInputs in;
    in.mass_kg = 0.5;
    in.cda_at_time = ConstCda(0.030);
    in.release_height_agl_m = 50.0;
    // Heading 000 = North.
    in.initial_velocity = EnuVector{0.0, 5.0, 0.0};
    in.wind_at_height = [alpha, z_ref, wind_from_rad](double h) {
        const double speed = wind::SpeedAtHeight(5.0, z_ref, h, alpha);
        return wind::ToVector(speed, wind_from_rad);
    };
    in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 38.0);

    const auto result = ballistics::IntegrateForward(in);
    REQUIRE(result.converged);
    REQUIRE_REL_NEAR(result.impact_displacement.e, 9.44, 0.02);
    REQUIRE_REL_NEAR(result.impact_displacement.n, 10.88, 0.02);
    const double total = std::sqrt(result.impact_displacement.e * result.impact_displacement.e +
                                    result.impact_displacement.n * result.impact_displacement.n);
    REQUIRE_REL_NEAR(total, 14.40, 0.02);
    REQUIRE_REL_NEAR(result.fall_time_s, 4.19, 0.02);
}

// Regression vector 4: terminal velocity, same payload and density (CdA
// 0.030, density from vector 3) -> 16.97 m/s, 0.5% tolerance.
TEST_CASE("Regression 4: terminal velocity") {
    const double rho = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 38.0);
    const double vt = ballistics::TerminalVelocity(0.5, 0.030, rho);
    REQUIRE_REL_NEAR(vt, 16.97, 0.005);
}

// Regression vector 5: hover, no wind -> displacement < 1 cm, fall ~4.13 s.
TEST_CASE("Regression 5: hover release, no wind, negligible drift") {
    ballistics::ForwardInputs in;
    in.mass_kg = 0.5;
    in.cda_at_time = ConstCda(0.030);
    in.release_height_agl_m = 50.0;
    in.initial_velocity = EnuVector{0.0, 0.0, 0.0};
    in.wind_at_height = NoWind();
    in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 15.0);

    const auto result = ballistics::IntegrateForward(in);
    REQUIRE(result.converged);
    const double drift = std::sqrt(result.impact_displacement.e * result.impact_displacement.e +
                                    result.impact_displacement.n * result.impact_displacement.n);
    REQUIRE(drift < 0.01);
    REQUIRE_REL_NEAR(result.fall_time_s, 4.13, 0.02);
}

// Regression vector 6: symmetry. Reversing wind bearing 180 deg mirrors
// displacement; reversing heading mirrors cross-track.
TEST_CASE("Regression 6: symmetry under wind bearing reversal") {
    auto run = [](double wind_from_deg) {
        ballistics::ForwardInputs in;
        in.mass_kg = 0.5;
        in.cda_at_time = ConstCda(0.030);
        in.release_height_agl_m = 50.0;
        in.initial_velocity = EnuVector{0.0, 5.0, 0.0};
        const double wind_from_rad = wind_from_deg * kPi / 180.0;
        in.wind_at_height = [wind_from_rad](double h) {
            const double speed = wind::SpeedAtHeight(5.0, 50.0, h, 0.14);
            return wind::ToVector(speed, wind_from_rad);
        };
        in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 20.0);
        return ballistics::IntegrateForward(in);
    };

    const auto a = run(270.0);
    const auto b = run(90.0);  // reversed bearing
    REQUIRE_REL_NEAR(a.impact_displacement.e, -b.impact_displacement.e, 0.01);
    REQUIRE_REL_NEAR(a.impact_displacement.n, b.impact_displacement.n, 0.01);
}

TEST_CASE("Regression 6b: symmetry under heading reversal") {
    auto run = [](double heading_sign) {
        ballistics::ForwardInputs in;
        in.mass_kg = 0.5;
        in.cda_at_time = ConstCda(0.030);
        in.release_height_agl_m = 50.0;
        in.initial_velocity = EnuVector{0.0, heading_sign * 5.0, 0.0};
        in.wind_at_height = [](double h) {
            const double speed = wind::SpeedAtHeight(5.0, 50.0, h, 0.14);
            return wind::ToVector(speed, 270.0 * kPi / 180.0);
        };
        in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 20.0);
        return ballistics::IntegrateForward(in);
    };

    const auto north = run(1.0);
    const auto south = run(-1.0);
    // Cross-track (E) component should be similar in magnitude; along-track
    // (N) mirrors sign with heading.
    REQUIRE_REL_NEAR(north.impact_displacement.n, -south.impact_displacement.n, 0.05);
}

// Property test: fall time decreases with height.
TEST_CASE("Property: fall time decreases with height") {
    auto fall_time = [](double h) {
        ballistics::ForwardInputs in;
        in.mass_kg = 0.5;
        in.cda_at_time = ConstCda(0.03);
        in.release_height_agl_m = h;
        in.initial_velocity = EnuVector{0, 0, 0};
        in.wind_at_height = NoWind();
        in.air_density_kg_m3 = 1.2;
        return ballistics::IntegrateForward(in).fall_time_s;
    };
    REQUIRE(fall_time(30.0) < fall_time(90.0));
}

// Property test: displacement grows with wind.
TEST_CASE("Property: displacement grows with wind speed") {
    auto drift = [](double wind_speed) {
        ballistics::ForwardInputs in;
        in.mass_kg = 0.5;
        in.cda_at_time = ConstCda(0.03);
        in.release_height_agl_m = 50.0;
        in.initial_velocity = EnuVector{0, 5.0, 0};
        in.wind_at_height = [wind_speed](double h) {
            const double s = wind::SpeedAtHeight(wind_speed, 50.0, h, 0.14);
            return wind::ToVector(s, 270.0 * kPi / 180.0);
        };
        in.air_density_kg_m3 = 1.2;
        const auto r = ballistics::IntegrateForward(in);
        return std::sqrt(r.impact_displacement.e * r.impact_displacement.e +
                          r.impact_displacement.n * r.impact_displacement.n);
    };
    REQUIRE(drift(2.0) < drift(10.0));
}

// Property test: higher ballistic coefficient (lower CdA) means less drift.
TEST_CASE("Property: lower CdA (denser ballistic coefficient) reduces drift") {
    auto drift = [](double cda) {
        ballistics::ForwardInputs in;
        in.mass_kg = 0.5;
        in.cda_at_time = ConstCda(cda);
        in.release_height_agl_m = 50.0;
        in.initial_velocity = EnuVector{0, 5.0, 0};
        in.wind_at_height = [](double h) {
            const double s = wind::SpeedAtHeight(6.0, 50.0, h, 0.14);
            return wind::ToVector(s, 270.0 * kPi / 180.0);
        };
        in.air_density_kg_m3 = 1.2;
        const auto r = ballistics::IntegrateForward(in);
        return std::sqrt(r.impact_displacement.e * r.impact_displacement.e +
                          r.impact_displacement.n * r.impact_displacement.n);
    };
    REQUIRE(drift(0.01) < drift(0.08));
}

TEST_CASE("Inverse solver converges on a reachable target") {
    ballistics::InverseInputs in;
    in.mass_kg = 0.5;
    in.cda_at_time = ConstCda(0.03);
    in.aircraft_position = EnuPoint{0.0, 0.0, 50.0};
    in.aircraft_velocity = EnuVector{0.0, 5.0, 0.0};
    in.target = EnuPoint{9.44, 10.88, 0.0};
    in.wind_at_height = [](double h) {
        const double s = wind::SpeedAtHeight(5.0, 50.0, h, 0.14);
        return wind::ToVector(s, 270.0 * kPi / 180.0);
    };
    in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 38.0);

    const auto result = ballistics::SolveInverse(in);
    REQUIRE(result.converged);
    REQUIRE(result.iterations <= 10);
}
