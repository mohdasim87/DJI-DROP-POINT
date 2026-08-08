// core/dispersion.h
//
// Monte Carlo dispersion estimate run alongside every deterministic solve.
// See spec section 6.6.
#pragma once

#include <cstdint>

#include "ballistics.h"
#include "math_constants.h"
#include "solution.h"

namespace droppoint::core::dispersion {

inline constexpr int kDefaultSamples = 200;

struct MonteCarloInputs {
    // Deterministic baseline, reused as the mean of every perturbed run.
    ballistics::ForwardInputs baseline;
    double wind_speed_mps = 0.0;
    double wind_from_bearing_rad = 0.0;
    double wind_gust_sigma_mps = 0.0;   // half the gust spread, per spec
    double wind_bearing_sigma_rad = 10.0 * kPi / 180.0;  // 10 deg
    double cda_sigma_fraction = 0.10;   // 10% of nominal CdA
    double release_timing_sigma_s = 0.12;
    double ground_speed_mps = 0.0;      // used to convert timing sigma to along-track distance
    double wind_reference_height_m = 0.0;  // z_ref for the power-law profile (baseline.release_height_agl_m)
    double wind_alpha = 0.14;              // power-law exponent, see wind::AlphaForSurface
    int samples = kDefaultSamples;
    std::uint64_t seed = 42;            // fixed seed for reproducible tests
};

// Runs `samples` perturbed forward integrations and reports the 50th/90th
// percentile radial miss relative to the deterministic baseline impact
// point. `wind_at_height_for` receives a perturbed (speed, bearing) pair and
// must return the height-dependent wind function (i.e. it re-applies the
// profile exponent already captured in baseline generation); callers
// typically pass wind::SpeedAtHeight + wind::ToVector bound with the
// profile's alpha and z_ref.
DispersionResult RunMonteCarlo(const MonteCarloInputs& in);

}  // namespace droppoint::core::dispersion
