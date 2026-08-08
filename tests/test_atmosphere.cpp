#include "atmosphere.h"
#include "test_framework.h"

using namespace droppoint::core;

// Regression vector 3: air density, sea level, 38 C -> 1.1345 kg/m^3, 0.1% tol.
TEST_CASE("Regression 3: air density sea level 38C") {
    const double rho = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), 38.0);
    REQUIRE_REL_NEAR(rho, 1.1345, 0.001);
}

TEST_CASE("Atmosphere: pressure decreases with altitude") {
    REQUIRE(atmosphere::PressurePa(1000.0) < atmosphere::PressurePa(0.0));
}
