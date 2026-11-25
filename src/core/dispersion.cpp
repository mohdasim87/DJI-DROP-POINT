#include "dispersion.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "geodesy.h"
#include "normal_sampler.h"
#include "wind.h"

namespace droppoint::core::dispersion {

DispersionResult RunMonteCarlo(const MonteCarloInputs& in) {
    std::mt19937_64 rng(in.seed);
    NormalSampler wind_speed_noise(0.0, in.wind_gust_sigma_mps);
    NormalSampler wind_bearing_noise(0.0, in.wind_bearing_sigma_rad);
    NormalSampler cda_noise(1.0, in.cda_sigma_fraction);
    NormalSampler timing_noise(0.0, in.release_timing_sigma_s);

    // Deterministic baseline impact point (release at e=n=0 in the forward
    // integrator's own frame).
    ballistics::ForwardInputs baseline = in.baseline;
    BallisticResult base_result = ballistics::IntegrateForward(baseline);
    const double base_e = base_result.impact_displacement.e;
    const double base_n = base_result.impact_displacement.n;

    std::vector<double> radial_misses;
    radial_misses.reserve(in.samples);

    for (int i = 0; i < in.samples; ++i) {
        const double speed = std::max(0.0, in.wind_speed_mps + wind_speed_noise(rng));
        const double bearing = geodesy::WrapAngleRad(in.wind_from_bearing_rad + wind_bearing_noise(rng));
        const double cda_scale = std::max(0.05, cda_noise(rng));
        const double timing_shift_s = timing_noise(rng);
        const double along_track_shift_m = timing_shift_s * in.ground_speed_mps;

        ballistics::ForwardInputs perturbed = in.baseline;
        const double z_ref = in.wind_reference_height_m;
        const double alpha = in.wind_alpha;
        perturbed.wind_at_height = [speed, bearing, z_ref, alpha](double h) {
            const double speed_at_h = wind::SpeedAtHeight(speed, z_ref, h, alpha);
            return wind::ToVector(speed_at_h, bearing);
        };

        auto scaled_cda = in.baseline.cda_at_time;
        perturbed.cda_at_time = [scaled_cda, cda_scale](double t) { return scaled_cda(t) * cda_scale; };

        // Shift the initial horizontal velocity along its own direction to
        // emulate a release-timing perturbation converted to an along-track
        // distance error, applied as an equivalent initial-position offset
        // rather than re-running the aircraft trajectory.
        BallisticResult sample = ballistics::IntegrateForward(perturbed);
        const double speed_h = std::sqrt(in.baseline.initial_velocity.e * in.baseline.initial_velocity.e +
                                          in.baseline.initial_velocity.n * in.baseline.initial_velocity.n);
        double shift_e = 0.0, shift_n = 0.0;
        if (speed_h > 1e-6) {
            shift_e = (in.baseline.initial_velocity.e / speed_h) * along_track_shift_m;
            shift_n = (in.baseline.initial_velocity.n / speed_h) * along_track_shift_m;
        }

        const double impact_e = sample.impact_displacement.e + shift_e;
        const double impact_n = sample.impact_displacement.n + shift_n;
        const double dE = impact_e - base_e;
        const double dN = impact_n - base_n;
        radial_misses.push_back(std::sqrt(dE * dE + dN * dN));
    }

    std::sort(radial_misses.begin(), radial_misses.end());
    DispersionResult result;
    result.samples = radial_misses.size();
    if (!radial_misses.empty()) {
        auto pct = [&](double p) {
            const double idx = p * (radial_misses.size() - 1);
            const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
            const std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
            const double frac = idx - static_cast<double>(lo);
            return radial_misses[lo] + frac * (radial_misses[hi] - radial_misses[lo]);
        };
        result.p50_radial_m = pct(0.50);
        result.p90_radial_m = pct(0.90);
    }
    return result;
}

}  // namespace droppoint::core::dispersion
