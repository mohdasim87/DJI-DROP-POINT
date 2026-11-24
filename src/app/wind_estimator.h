// app/wind_estimator.h
//
// Rolling-window wind estimator: the only wind source on aircraft without
// a wind telemetry topic (docs/OPEN-QUESTIONS.md #2). Feeds hover-attitude
// samples through core::wind::EstimateFromHover, keeps a bounded window,
// and reports a vector-averaged wind plus a gust sigma. Pure logic, no HAL
// and no I/O: every input is passed in per call so it is unit-testable.
#pragma once

#include <cstdint>
#include <deque>

#include "core/solution.h"

namespace droppoint::app {

struct WindEstimatorConfig {
    double total_mass_kg = 0.0;   // aircraft + equipment + active payload
    double cda_airframe = 0.0;    // m^2, from aircraft.yaml
    double max_ground_speed_mps = 0.5;  // hover-estimate validity bound (spec 6.3)
    double max_accel_mps2 = 0.5;
    double window_s = 10.0;       // rolling averaging window
    double min_hover_s = 3.0;     // minimum hover span inside the window before an estimate is valid
    double max_age_s = 300.0;     // an estimate this old is treated as absent
};

class WindEstimator {
public:
    explicit WindEstimator(WindEstimatorConfig config) : config_(config) {}

    // Total hover mass changes when the active payload changes.
    void SetTotalMassKg(double mass_kg) { config_.total_mass_kg = mass_kg; }

    // Adds a sample only while the airframe qualifies as "hovering":
    // state.valid, horizontal ground speed < max_ground_speed_mps, and
    // |accel| < max_accel_mps2. Samples containing NaN/inf are ignored.
    // Handles now_us going backwards (window is cleared).
    void Update(const core::AircraftState& state, double air_density_kg_m3, double accel_mps2,
                std::uint64_t now_us);

    // Vector-mean wind over the newest contiguous hover segment (sample
    // gaps > 0.3 s split segments). Valid when that segment covers
    // >= min_hover_s and its newest sample is <= max_age_s old; a
    // still-collecting new segment does not disqualify the last good one.
    // Always reports from_telemetry_topic = false.
    core::WindSample Current(std::uint64_t now_us) const;

    // Standard deviation of sample wind speeds inside the window
    // (0.0 with fewer than two samples).
    double GustSigmaMps() const;

    // Mean aircraft altitude (ENU u) over the window samples: the height
    // the hover estimate refers to, used as the wind-profile z_ref.
    double ReferenceU() const;

    // Age of the reported estimate, seconds. Large when none is reportable.
    double AgeS(std::uint64_t now_us) const;

private:
    struct Sample {
        std::uint64_t timestamp_us = 0;
        double toward_e = 0.0;  // ENU vector the wind blows TOWARD
        double toward_n = 0.0;
        double speed_mps = 0.0;
        double u_m = 0.0;
    };

    static std::uint64_t CoverageUs(const std::deque<Sample>& seg);
    void Prune();
    const std::deque<Sample>* ReportedSegment(std::uint64_t now_us) const;

    WindEstimatorConfig config_;
    // samples_ holds the CURRENT contiguous hover segment (a >0.3 s gap
    // ends it); archived_ keeps the previous qualifying segment for the
    // fallback described on Current(). Both are bounded to the window.
    std::deque<Sample> samples_;
    std::deque<Sample> archived_;
};

}  // namespace droppoint::app
