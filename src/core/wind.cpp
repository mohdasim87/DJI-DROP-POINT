#include "wind.h"

#include <algorithm>
#include <cmath>

#include "geodesy.h"
#include "math_constants.h"

namespace droppoint::core::wind {

namespace {
constexpr double kMinHeightForProfile = 0.5;  // m AGL
constexpr double kGravity = 9.80665;          // m/s^2
constexpr double kDisagreementSpeedThresholdMps = 2.0;
constexpr double kDisagreementBearingThresholdRad = 30.0 * kPi / 180.0;
}  // namespace

double AlphaForSurface(SurfaceType surface) {
    switch (surface) {
        case SurfaceType::kOpenWaterOrFlatDesert:
            return 0.10;
        case SurfaceType::kOpenCountry:
            return 0.14;
        case SurfaceType::kBrokenCountry:
            return 0.20;
        case SurfaceType::kUrbanOrForest:
            return 0.27;
    }
    return 0.14;
}

double SpeedAtHeight(double v_ref_mps, double z_ref_m, double z_m, double alpha) {
    if (z_m <= kMinHeightForProfile) return 0.0;
    return v_ref_mps * std::pow(z_m / z_ref_m, alpha);
}

EnuVector ToVector(double speed_mps, double from_bearing_rad) {
    // Meteorological bearing (0 = North, clockwise) the wind comes FROM; it
    // blows TOWARD from_bearing + 180 deg. Convert compass bearing to ENU.
    const double toward_bearing = geodesy::WrapAngleRad(from_bearing_rad + kPi);
    EnuVector v;
    v.e = speed_mps * std::sin(toward_bearing);
    v.n = speed_mps * std::cos(toward_bearing);
    v.u = 0.0;
    return v;
}

WindSample EstimateFromHover(const HoverEstimateInputs& in) {
    const double theta = std::sqrt(in.pitch_rad * in.pitch_rad + in.roll_rad * in.roll_rad);
    WindSample out;
    out.from_telemetry_topic = false;

    if (in.cda_airframe <= 0.0 || in.air_density_kg_m3 <= 0.0) {
        out.valid = false;
        return out;
    }

    const double tan_theta = std::tan(theta);
    if (tan_theta < 0.0) {
        out.valid = false;
        return out;
    }

    const double speed_sq =
        2.0 * in.mass_kg * kGravity * tan_theta / (in.air_density_kg_m3 * in.cda_airframe);
    out.speed_mps = speed_sq > 0.0 ? std::sqrt(speed_sq) : 0.0;

    // Direction the aircraft tilts into: pitch forward (nose down, positive
    // pitch by convention) tilts into the aircraft heading; roll tilts
    // laterally. Combine as a vector in the body frame then rotate to ENU by
    // yaw. Wind blows TOWARD this direction (the tilt is the airframe
    // leaning into the oncoming wind to hold position), so this bearing is
    // directly usable as the "toward" bearing, and we store its reciprocal
    // as the meteorological "from" bearing for consistency with the rest of
    // the system.
    const double tilt_dir_body = std::atan2(in.roll_rad, in.pitch_rad);
    const double toward_bearing_enu_math = in.yaw_rad + tilt_dir_body;
    // Convert math-convention (CCW from East) heading sum to compass bearing
    // (CW from North): compass = 90 - math, then add pi to get "from".
    const double toward_compass = geodesy::WrapAngleRad(kPi / 2.0 - toward_bearing_enu_math);
    out.from_bearing_rad = geodesy::WrapAngleRad(toward_compass + kPi);
    out.valid = true;
    return out;
}

WindDisagreement CompareWindSources(const WindSample& telemetry, const WindSample& hover_estimate) {
    WindDisagreement result;
    if (!telemetry.valid || !hover_estimate.valid) {
        return result;
    }

    result.speed_diff_mps = std::abs(telemetry.speed_mps - hover_estimate.speed_mps);
    double bearing_diff = geodesy::WrapAngleRad(telemetry.from_bearing_rad - hover_estimate.from_bearing_rad);
    result.bearing_diff_rad = std::abs(bearing_diff);

    result.disagrees = result.speed_diff_mps > kDisagreementSpeedThresholdMps ||
                        result.bearing_diff_rad > kDisagreementBearingThresholdRad;

    if (result.disagrees) {
        result.recommended =
            (telemetry.speed_mps >= hover_estimate.speed_mps) ? telemetry : hover_estimate;
    } else {
        result.recommended = telemetry;
    }
    return result;
}

}  // namespace droppoint::core::wind
