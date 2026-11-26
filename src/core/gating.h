// core/gating.h
//
// Hard/soft gate evaluation, spec section 7. Pure function of a snapshot of
// inputs -> a set of gate results; core/ never reads config files or clocks
// itself, the caller (app/) supplies already-resolved thresholds and
// current values every cycle.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math_constants.h"
#include "solution.h"

namespace droppoint::core::gating {

enum class GateId {
    kNoTarget,
    kTargetStale,
    kDepressionAngleLow,       // warn band
    kDepressionAngleTooLow,    // refuse band
    kCrosswindExceeded,
    kAglBelowMinimum,
    kGnssSatellitesLow,
    kRtkNotFix,
    kTelemetryStale,
    kAccelerationExcessive,
    kNoTerrainElevation,
    kInverseSolverFailed,
    kSolutionDivergence,
    kGustSigmaHigh,
    kAglAboveNominal,
    kAglAboveMaximum,
    kRtkFloat,
    kPayloadUncalibrated,
    kTargetElevationRawDem,
    kNoDropPolygonViolation,
    kNoWindEstimate,
    kWindModelUncalibrated,
    kLicenseExpired,
};

enum class GateSeverity { kHard, kSoft };

struct GateResult {
    GateId id;
    GateSeverity severity;
    bool tripped = false;
    std::string message;  // human-readable, safe to surface verbatim in the floating window
};

// Thresholds, all config-driven (system.yaml / payloads.yaml), never
// hardcoded per spec section 7's closing line.
struct Thresholds {
    double target_max_age_s = 120.0;
    double depression_warn_rad = 30.0 * kPi / 180.0;
    double depression_refuse_rad = 15.0 * kPi / 180.0;
    double crosswind_limit_mps = 1e9;    // from payload profile
    double min_release_agl_m = 0.0;      // from payload profile
    double max_release_agl_m = 1e9;      // from payload profile
    double nominal_max_agl_m = 90.0;
    int min_gnss_satellites = 12;
    bool rtk_required = false;
    double max_telemetry_age_s = 0.400;
    double max_acceleration_mps2 = 0.5;
    double max_solution_divergence_m = 25.0;
    double gust_sigma_warn_mps = 2.0;
};

enum class RtkStatus { kNotAvailable, kFloat, kFix };

// One evaluation cycle's worth of live inputs, already resolved by app/.
struct Inputs {
    bool target_latched = false;
    double target_age_s = 0.0;
    bool target_confirmed = false;
    double depression_angle_rad = 0.0;
    double crosswind_mps = 0.0;
    double agl_m = 0.0;
    int gnss_satellites = 0;
    RtkStatus rtk_status = RtkStatus::kNotAvailable;
    double telemetry_age_s = 0.0;
    double acceleration_mps2 = 0.0;
    bool terrain_elevation_available = false;
    bool inverse_solver_converged = true;
    double predicted_impact_divergence_m = 0.0;
    double gust_sigma_mps = 0.0;
    bool payload_calibrated = true;
    ElevationSource target_elevation_source = ElevationSource::kLrfCorrected;
    bool predicted_impact_in_no_drop_polygon = false;
    bool wind_valid = true;               // active wind source (telemetry topic or estimator) produced a sample
    bool wind_model_calibrated = true;    // aircraft CdA/mass calibration state, soft gate
    bool license_ok = true;               // hw::LicenseStatus says arming is allowed (trial/licensed)
};

struct Evaluation {
    std::vector<GateResult> results;
    bool any_hard_tripped = false;
    bool any_soft_tripped = false;
    // First tripped hard gate's message, used directly as the floating
    // window "what to do about it" line, per spec 9.
    std::string primary_hold_reason;
};

Evaluation Evaluate(const Inputs& in, const Thresholds& th);

}  // namespace droppoint::core::gating
