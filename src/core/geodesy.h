// core/geodesy.h
//
// WGS-84 geodetic <-> local East-North-Up tangent-plane conversions, plus
// great-circle-adjacent range/bearing helpers used for target confirmation
// (distance and bearing from the aircraft to the latched target).
//
// The local ENU frame used throughout core/ is defined by a single WGS-84
// origin (typically the aircraft's position at boot or at the first valid
// fix). All EnuPoint values in solution.h are relative to that origin.
#pragma once

#include "solution.h"

namespace droppoint::core::geodesy {

// WGS-84 ellipsoid constants.
inline constexpr double kWgs84A = 6378137.0;            // semi-major axis, m
inline constexpr double kWgs84F = 1.0 / 298.257223563;  // flattening
inline constexpr double kWgs84B = kWgs84A * (1.0 - kWgs84F);
inline constexpr double kWgs84E2 = kWgs84F * (2.0 - kWgs84F);

// Converts a geodetic coordinate to ECEF (metres).
struct Ecef {
    double x = 0.0, y = 0.0, z = 0.0;
};
Ecef LlaToEcef(const LatLonAlt& lla);
LatLonAlt EcefToLla(const Ecef& ecef);

// A local tangent-plane origin. Construct once per flight (or when the
// aircraft moves far enough that flat-earth error becomes significant).
struct EnuOrigin {
    LatLonAlt lla;
    Ecef ecef;
};
EnuOrigin MakeEnuOrigin(const LatLonAlt& lla);

EnuPoint LlaToEnu(const LatLonAlt& lla, const EnuOrigin& origin);
LatLonAlt EnuToLla(const EnuPoint& enu, const EnuOrigin& origin);

// Great-circle-adjacent flat-earth range/bearing between two ENU points.
// Valid at the scales this system operates at (target ranges up to ~500 m).
double HorizontalRange(const EnuPoint& a, const EnuPoint& b);
// Bearing from a to b, radians, 0 = North, clockwise positive (compass
// convention), matching floating-window and audio "come round to" cues.
// Returned in [0, 2*pi).
double BearingRad(const EnuPoint& a, const EnuPoint& b);

// Normalises an angle to (-pi, pi].
double WrapAngleRad(double angle_rad);

// Normalises a compass bearing (0 = North, clockwise positive) to [0, 2*pi).
double NormalizeCompassBearingRad(double bearing_rad);

}  // namespace droppoint::core::geodesy
