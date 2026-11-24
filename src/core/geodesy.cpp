#include "geodesy.h"

#include <cmath>

#include "math_constants.h"

namespace droppoint::core::geodesy {

namespace {
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
}  // namespace

Ecef LlaToEcef(const LatLonAlt& lla) {
    const double lat = lla.lat_deg * kDegToRad;
    const double lon = lla.lon_deg * kDegToRad;
    const double sin_lat = std::sin(lat);
    const double cos_lat = std::cos(lat);
    const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);

    Ecef out;
    out.x = (n + lla.alt_m_hae) * cos_lat * std::cos(lon);
    out.y = (n + lla.alt_m_hae) * cos_lat * std::sin(lon);
    out.z = (n * (1.0 - kWgs84E2) + lla.alt_m_hae) * sin_lat;
    return out;
}

LatLonAlt EcefToLla(const Ecef& ecef) {
    // Bowring's method, a handful of iterations converges to sub-mm.
    const double x = ecef.x, y = ecef.y, z = ecef.z;
    const double lon = std::atan2(y, x);
    const double p = std::sqrt(x * x + y * y);
    double lat = std::atan2(z, p * (1.0 - kWgs84E2));
    double alt = 0.0;
    for (int i = 0; i < 8; ++i) {
        const double sin_lat = std::sin(lat);
        const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);
        alt = p / std::cos(lat) - n;
        lat = std::atan2(z, p * (1.0 - kWgs84E2 * n / (n + alt)));
    }
    LatLonAlt out;
    out.lat_deg = lat * kRadToDeg;
    out.lon_deg = lon * kRadToDeg;
    out.alt_m_hae = alt;
    return out;
}

EnuOrigin MakeEnuOrigin(const LatLonAlt& lla) {
    EnuOrigin origin;
    origin.lla = lla;
    origin.ecef = LlaToEcef(lla);
    return origin;
}

EnuPoint LlaToEnu(const LatLonAlt& lla, const EnuOrigin& origin) {
    const Ecef p = LlaToEcef(lla);
    const double dx = p.x - origin.ecef.x;
    const double dy = p.y - origin.ecef.y;
    const double dz = p.z - origin.ecef.z;

    const double lat0 = origin.lla.lat_deg * kDegToRad;
    const double lon0 = origin.lla.lon_deg * kDegToRad;
    const double sin_lat = std::sin(lat0), cos_lat = std::cos(lat0);
    const double sin_lon = std::sin(lon0), cos_lon = std::cos(lon0);

    EnuPoint enu;
    enu.e = -sin_lon * dx + cos_lon * dy;
    enu.n = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
    enu.u = cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz;
    return enu;
}

LatLonAlt EnuToLla(const EnuPoint& enu, const EnuOrigin& origin) {
    const double lat0 = origin.lla.lat_deg * kDegToRad;
    const double lon0 = origin.lla.lon_deg * kDegToRad;
    const double sin_lat = std::sin(lat0), cos_lat = std::cos(lat0);
    const double sin_lon = std::sin(lon0), cos_lon = std::cos(lon0);

    const double dx = -sin_lon * enu.e - sin_lat * cos_lon * enu.n + cos_lat * cos_lon * enu.u;
    const double dy = cos_lon * enu.e - sin_lat * sin_lon * enu.n + cos_lat * sin_lon * enu.u;
    const double dz = cos_lat * enu.n + sin_lat * enu.u;

    Ecef p;
    p.x = origin.ecef.x + dx;
    p.y = origin.ecef.y + dy;
    p.z = origin.ecef.z + dz;
    return EcefToLla(p);
}

double HorizontalRange(const EnuPoint& a, const EnuPoint& b) {
    const double de = b.e - a.e;
    const double dn = b.n - a.n;
    return std::sqrt(de * de + dn * dn);
}

double BearingRad(const EnuPoint& a, const EnuPoint& b) {
    const double de = b.e - a.e;
    const double dn = b.n - a.n;
    // Compass bearing: 0 = North, clockwise positive. atan2 in ENU with
    // (East, North) args gives exactly that convention.
    return NormalizeCompassBearingRad(std::atan2(de, dn));
}

double WrapAngleRad(double angle_rad) {
    while (angle_rad > kPi) angle_rad -= 2.0 * kPi;
    while (angle_rad <= -kPi) angle_rad += 2.0 * kPi;
    return angle_rad;
}

double NormalizeCompassBearingRad(double bearing_rad) {
    double b = std::fmod(bearing_rad, 2.0 * kPi);
    if (b < 0.0) b += 2.0 * kPi;
    return b;
}

}  // namespace droppoint::core::geodesy
