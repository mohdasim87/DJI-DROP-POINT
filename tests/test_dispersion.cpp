#include "atmosphere.h"
#include "dispersion.h"
#include "math_constants.h"
#include "test_framework.h"

using namespace droppoint::core;

TEST_CASE("Dispersion: fixed seed is reproducible") {
    dispersion::MonteCarloInputs in;
    in.baseline.mass_kg = 0.5;
    in.baseline.cda_at_time = [](double) { return 0.03; };
    in.baseline.release_height_agl_m = 50.0;
    in.baseline.initial_velocity = EnuVector{0.0, 5.0, 0.0};
    in.baseline.wind_at_height = [](double) { return EnuVector{1.0, 0.0, 0.0}; };
    in.baseline.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 20.0);
    in.wind_speed_mps = 5.0;
    in.wind_from_bearing_rad = 270.0 * kPi / 180.0;
    in.wind_gust_sigma_mps = 1.0;
    in.ground_speed_mps = 5.0;
    in.wind_reference_height_m = 50.0;
    in.wind_alpha = 0.14;
    in.samples = 200;
    in.seed = 42;

    const auto r1 = dispersion::RunMonteCarlo(in);
    const auto r2 = dispersion::RunMonteCarlo(in);
    REQUIRE_NEAR(r1.p50_radial_m, r2.p50_radial_m, 1e-12);
    REQUIRE_NEAR(r1.p90_radial_m, r2.p90_radial_m, 1e-12);
    REQUIRE(r1.samples == 200);
    REQUIRE(r1.p90_radial_m >= r1.p50_radial_m);
}

TEST_CASE("Dispersion: larger gust sigma increases spread") {
    auto run = [](double gust_sigma) {
        dispersion::MonteCarloInputs in;
        in.baseline.mass_kg = 0.5;
        in.baseline.cda_at_time = [](double) { return 0.03; };
        in.baseline.release_height_agl_m = 50.0;
        in.baseline.initial_velocity = EnuVector{0.0, 5.0, 0.0};
        in.baseline.wind_at_height = [](double) { return EnuVector{1.0, 0.0, 0.0}; };
        in.baseline.air_density_kg_m3 = 1.2;
        in.wind_speed_mps = 5.0;
        in.wind_from_bearing_rad = 270.0 * kPi / 180.0;
        in.wind_gust_sigma_mps = gust_sigma;
        in.ground_speed_mps = 5.0;
        in.wind_reference_height_m = 50.0;
        in.wind_alpha = 0.14;
        in.samples = 500;
        in.seed = 7;
        return dispersion::RunMonteCarlo(in);
    };
    const auto low = run(0.2);
    const auto high = run(4.0);
    REQUIRE(high.p90_radial_m > low.p90_radial_m);
}
