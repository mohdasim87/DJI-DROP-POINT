// core/normal_sampler.h — see below.
#pragma once

#include <cmath>
#include <random>

namespace droppoint::core {

// std::normal_distribution with sigma <= 0 or non-finite is a precondition
// violation: MSVC Debug aborts the process via a CRT assertion, other
// standard libraries are UB. A zero sigma is a legitimate "no noise"
// request (e.g. steady wind, jitter disabled) — this wrapper then returns
// the mean WITHOUT touching the RNG. For a valid sigma it holds a real
// distribution and draws normally, so the seeded draw stream — and thus
// reproducibility of fixed-seed results — is identical to a bare
// std::normal_distribution.
class NormalSampler {
public:
    NormalSampler(double mean, double sigma)
        : mean_(mean),
          valid_(sigma > 0.0 && std::isfinite(sigma)),
          dist_(mean, valid_ ? sigma : 1.0) {}

    template <typename Rng>
    double operator()(Rng& rng) {
        return valid_ ? dist_(rng) : mean_;
    }

private:
    double mean_ = 0.0;
    bool valid_ = false;
    std::normal_distribution<double> dist_;
};

}  // namespace droppoint::core
