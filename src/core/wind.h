// core/wind.h
//
// Wind profile power law and the hover-attitude wind-estimation fallback.
// See spec sections 6.2 and 6.3.
#pragma once

#include "solution.h"

namespace droppoint::core::wind {

enum class SurfaceType { kOpenWaterOrFlatDesert, kOpenCountry, kBrokenCountry, kUrbanOrForest };

// Power-law exponent (alpha) per surface roughness class. Default is
// kOpenCountry (0.14) per spec.
double AlphaForSurface(SurfaceType surface);

// Wind speed at height z (metres AGL) given a reference speed at reference
// height, per:
//   V(z) = V_ref * (z / z_ref)^alpha      for z > 0.5 m, else 0
double SpeedAtHeight(double v_ref_mps, double z_ref_m, double z_m, double alpha);

// The ENU vector wind blows TOWARD, given a meteorological "from" bearing
// (the convention used throughout the config and UI: the bearing the wind
// comes FROM). Wind blows toward from_bearing + 180 deg.
EnuVector ToVector(double speed_mps, double from_bearing_rad);

// Hover-attitude wind estimator fallback (used when no wind telemetry topic
// exists, or continuously to cross-check a reported wind topic). Valid only
// below 0.5 m/s ground speed in a GNSS-held hover; caller is responsible for
// gating on that precondition, but this computes the physics regardless:
//   theta = sqrt(pitch^2 + roll^2)
//   V_wind = sqrt( 2 * m_aircraft * g * tan(theta) / (rho * CdA_airframe) )
// Bearing is the direction the aircraft tilts into (i.e. the direction the
// nose-down/roll vector points), which is also the direction the wind blows
// toward.
struct HoverEstimateInputs {
    double pitch_rad = 0.0;
    double roll_rad = 0.0;
    double yaw_rad = 0.0;  // aircraft heading, used to resolve tilt direction into ENU
    double mass_kg = 0.0;
    double cda_airframe = 0.0;
    double air_density_kg_m3 = 1.225;
};
WindSample EstimateFromHover(const HoverEstimateInputs& in);

// Compares a telemetry-reported wind sample against the hover-fallback
// estimate. Returns true (and sets disagreement fields) when they differ by
// more than 2 m/s or 30 degrees, per spec 6.3. When they disagree, the
// caller should surface a warning and use the larger-magnitude estimate.
struct WindDisagreement {
    bool disagrees = false;
    double speed_diff_mps = 0.0;
    double bearing_diff_rad = 0.0;
    WindSample recommended;  // the larger-magnitude of the two, when disagrees == true
};
WindDisagreement CompareWindSources(const WindSample& telemetry, const WindSample& hover_estimate);

}  // namespace droppoint::core::wind
