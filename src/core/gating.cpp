#include "gating.h"

namespace droppoint::core::gating {

namespace {
void Add(Evaluation& eval, GateId id, GateSeverity sev, bool tripped, std::string message) {
    GateResult r;
    r.id = id;
    r.severity = sev;
    r.tripped = tripped;
    r.message = std::move(message);
    if (tripped) {
        if (sev == GateSeverity::kHard) {
            eval.any_hard_tripped = true;
            if (eval.primary_hold_reason.empty()) eval.primary_hold_reason = r.message;
        } else {
            eval.any_soft_tripped = true;
        }
    }
    eval.results.push_back(std::move(r));
}
}  // namespace

Evaluation Evaluate(const Inputs& in, const Thresholds& th) {
    Evaluation eval;

    Add(eval, GateId::kNoTarget, GateSeverity::kHard, !in.target_latched,
        "NO TARGET - AIM AND PRESS SET TARGET");
    Add(eval, GateId::kTargetStale, GateSeverity::kHard,
        in.target_latched && in.target_age_s > th.target_max_age_s,
        "TARGET EXPIRED - RE-DESIGNATE");

    Add(eval, GateId::kDepressionAngleTooLow, GateSeverity::kHard,
        in.target_latched && in.depression_angle_rad < th.depression_refuse_rad,
        "DEPRESSION ANGLE TOO LOW - RE-DESIGNATE FROM STEEPER ANGLE");
    Add(eval, GateId::kDepressionAngleLow, GateSeverity::kSoft,
        in.target_latched && in.depression_angle_rad >= th.depression_refuse_rad &&
            in.depression_angle_rad < th.depression_warn_rad,
        "SHALLOW DESIGNATION ANGLE - ACCURACY DEGRADED");

    Add(eval, GateId::kCrosswindExceeded, GateSeverity::kHard,
        in.crosswind_mps > th.crosswind_limit_mps, "HOLD - CROSSWIND EXCEEDS PROFILE LIMIT");

    Add(eval, GateId::kAglBelowMinimum, GateSeverity::kHard, in.agl_m < th.min_release_agl_m,
        "HOLD - CLIMB, BELOW MINIMUM RELEASE AGL");

    Add(eval, GateId::kGnssSatellitesLow, GateSeverity::kHard,
        in.gnss_satellites < th.min_gnss_satellites, "HOLD - GNSS SATELLITE COUNT LOW");

    Add(eval, GateId::kRtkNotFix, GateSeverity::kHard,
        th.rtk_required && in.rtk_status != RtkStatus::kFix, "HOLD - RTK FIX REQUIRED");
    Add(eval, GateId::kRtkFloat, GateSeverity::kSoft,
        !th.rtk_required && in.rtk_status == RtkStatus::kFloat, "RTK FLOAT - POSITION ACCURACY REDUCED");

    Add(eval, GateId::kTelemetryStale, GateSeverity::kHard,
        in.telemetry_age_s > th.max_telemetry_age_s, "HOLD - TELEMETRY STALE");

    Add(eval, GateId::kAccelerationExcessive, GateSeverity::kHard,
        in.acceleration_mps2 > th.max_acceleration_mps2, "HOLD - STABILISE, ACCELERATION TOO HIGH");

    Add(eval, GateId::kNoTerrainElevation, GateSeverity::kHard, !in.terrain_elevation_available,
        "HOLD - NO TERRAIN ELEVATION AVAILABLE");

    Add(eval, GateId::kInverseSolverFailed, GateSeverity::kHard, !in.inverse_solver_converged,
        "HOLD - SOLVER DID NOT CONVERGE");

    Add(eval, GateId::kSolutionDivergence, GateSeverity::kHard,
        in.predicted_impact_divergence_m > th.max_solution_divergence_m,
        "ABORT - PREDICTED IMPACT DIVERGES FROM TARGET");

    Add(eval, GateId::kGustSigmaHigh, GateSeverity::kSoft,
        in.gust_sigma_mps > th.gust_sigma_warn_mps, "GUSTY - DISPERSION INCREASED");

    Add(eval, GateId::kAglAboveNominal, GateSeverity::kSoft, in.agl_m > th.nominal_max_agl_m,
        "HIGH AGL - DISPERSION INCREASED");

    Add(eval, GateId::kPayloadUncalibrated, GateSeverity::kSoft, !in.payload_calibrated,
        "PAYLOAD PROFILE UNCALIBRATED");

    Add(eval, GateId::kTargetElevationRawDem, GateSeverity::kSoft,
        in.target_elevation_source != ElevationSource::kLrfCorrected,
        "TGT ELEV FROM DEM ONLY - RANGE THE TARGET FIRST");

    Add(eval, GateId::kNoDropPolygonViolation, GateSeverity::kHard,
        in.predicted_impact_in_no_drop_polygon, "ABORT - IMPACT INSIDE NO-DROP POLYGON");

    return eval;
}

}  // namespace droppoint::core::gating
