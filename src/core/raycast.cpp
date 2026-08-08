#include "raycast.h"

#include <cmath>

namespace droppoint::core::raycast {

namespace {

struct Mat3 {
    double m[3][3];
};

Mat3 Multiply(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) sum += a.m[i][k] * b.m[k][j];
            out.m[i][j] = sum;
        }
    return out;
}

struct Vec3 {
    double x, y, z;
};

Vec3 Apply(const Mat3& m, const Vec3& v) {
    return Vec3{m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
                m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
                m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
}

Mat3 RotX(double a) {
    return Mat3{{{1, 0, 0}, {0, std::cos(a), -std::sin(a)}, {0, std::sin(a), std::cos(a)}}};
}
Mat3 RotY(double a) {
    return Mat3{{{std::cos(a), 0, std::sin(a)}, {0, 1, 0}, {-std::sin(a), 0, std::cos(a)}}};
}
Mat3 RotZ(double a) {
    return Mat3{{{std::cos(a), -std::sin(a), 0}, {std::sin(a), std::cos(a), 0}, {0, 0, 1}}};
}

}  // namespace

projection::NormalisedPoint Undistort(const projection::NormalisedPoint& distorted,
                                       const projection::Intrinsics& intr) {
    if (!intr.has_distortion) return distorted;

    double x = distorted.x;
    double y = distorted.y;
    const double x_orig = distorted.x;
    const double y_orig = distorted.y;

    for (int i = 0; i < 5; ++i) {
        const double r2 = x * x + y * y;
        const double r4 = r2 * r2;
        const double r6 = r4 * r2;
        const double radial = 1.0 + intr.k1 * r2 + intr.k2 * r4 + intr.k3 * r6;
        const double dx = 2.0 * intr.p1 * x * y + intr.p2 * (r2 + 2.0 * x * x);
        const double dy = intr.p1 * (r2 + 2.0 * y * y) + 2.0 * intr.p2 * x * y;
        x = (x_orig - dx) / radial;
        y = (y_orig - dy) / radial;
    }
    return projection::NormalisedPoint{x, y};
}

EnuVector CameraRayToEnu(const projection::CameraPoint& d_cam, const GimbalAttitude& gimbal) {
    // M: camera (x right, y down, z forward) -> body FRD (x forward, y
    // right, z down).
    const Vec3 body{d_cam.z, d_cam.x, d_cam.y};

    // v_ned = Rz(yaw) * Ry(pitch) * Rx(roll) * v_body
    const Mat3 rx = RotX(gimbal.roll_rad);
    const Mat3 ry = RotY(gimbal.pitch_rad);
    const Mat3 rz = RotZ(gimbal.yaw_rad);
    const Mat3 r = Multiply(rz, Multiply(ry, rx));
    const Vec3 ned = Apply(r, body);

    // NED (x=North, y=East, z=Down) -> ENU (E, N, U)
    EnuVector enu;
    enu.e = ned.y;
    enu.n = ned.x;
    enu.u = -ned.z;

    const double norm = std::sqrt(enu.e * enu.e + enu.n * enu.n + enu.u * enu.u);
    if (norm > 1e-12) {
        enu.e /= norm;
        enu.n /= norm;
        enu.u /= norm;
    }
    return enu;
}

RaycastResult CastPixelToGround(const RaycastInputs& in) {
    RaycastResult result;

    // Step 1: normalise with live zoom applied.
    const double fx = projection::FocalLengthPxX(in.intrinsics) * in.zoom_ratio;
    const double fy = projection::FocalLengthPxY(in.intrinsics) * in.zoom_ratio;
    projection::NormalisedPoint distorted{(in.u_px - in.intrinsics.c_x) / fx,
                                           (in.v_px - in.intrinsics.c_y) / fy};

    // Step 2: undistort (skips internally if no coefficients configured).
    const projection::NormalisedPoint undistorted = Undistort(distorted, in.intrinsics);

    // Step 3: ray in camera frame, normalised.
    const double norm = std::sqrt(undistorted.x * undistorted.x + undistorted.y * undistorted.y + 1.0);
    const projection::CameraPoint d_cam{undistorted.x / norm, undistorted.y / norm, 1.0 / norm};

    // Step 4: rotate to ENU.
    const EnuVector d_enu = CameraRayToEnu(d_cam, in.gimbal);

    // Step 5: reject bad rays (level or rising).
    if (d_enu.u >= in.horizon_reject_threshold) {
        result.hit = false;
        result.failure = RaycastFailure::kRayAboveHorizon;
        return result;
    }
    result.depression_angle_rad = std::asin(std::max(-1.0, std::min(1.0, -d_enu.u)));

    // Step 6: march along the ray, converting each sample to lat/lon and
    // comparing ray altitude to DEM elevation (bias-corrected if valid).
    const double origin_alt_abs = in.origin.lla.alt_m_hae;
    const double aircraft_alt_abs = origin_alt_abs + in.aircraft_position.u;

    auto ray_point_enu = [&](double s) {
        return EnuPoint{in.aircraft_position.e + d_enu.e * s, in.aircraft_position.n + d_enu.n * s,
                         in.aircraft_position.u + d_enu.u * s};
    };
    auto ray_altitude_abs = [&](double s) { return aircraft_alt_abs + d_enu.u * s; };

    terrain::DatumBias empty_bias;
    const terrain::DatumBias& bias_ref = in.bias ? *in.bias : empty_bias;

    double prev_s = 0.0;
    double prev_diff = 0.0;  // ray altitude - terrain elevation
    bool have_prev = false;
    ElevationSource last_source = ElevationSource::kUnknown;
    bool found_tile = false;

    const int max_steps = static_cast<int>(in.march_max_range_m / in.march_step_m) + 1;
    for (int i = 0; i <= max_steps; ++i) {
        const double s = std::min(static_cast<double>(i) * in.march_step_m, in.march_max_range_m);
        const EnuPoint p = ray_point_enu(s);
        const LatLonAlt lla = geodesy::EnuToLla(p, in.origin);
        const terrain::ElevationResult elev =
            terrain::QueryElevation(lla.lat_deg, lla.lon_deg, in.dem_fn, bias_ref, in.now_us, in.aircraft_position);

        if (!elev.available) {
            result.hit = false;
            result.failure = RaycastFailure::kNoDemTile;
            return result;
        }
        found_tile = true;
        last_source = elev.source;

        const double diff = ray_altitude_abs(s) - elev.elevation_m;
        if (have_prev && diff <= 0.0 && prev_diff > 0.0) {
            // Bisect between prev_s and s to converge under 1 cm.
            double lo = prev_s, hi = s;
            double lo_diff = prev_diff;
            for (int b = 0; b < in.bisection_iterations; ++b) {
                const double mid = 0.5 * (lo + hi);
                const EnuPoint mid_p = ray_point_enu(mid);
                const LatLonAlt mid_lla = geodesy::EnuToLla(mid_p, in.origin);
                const terrain::ElevationResult mid_elev = terrain::QueryElevation(
                    mid_lla.lat_deg, mid_lla.lon_deg, in.dem_fn, bias_ref, in.now_us, in.aircraft_position);
                if (!mid_elev.available) break;
                const double mid_diff = ray_altitude_abs(mid) - mid_elev.elevation_m;
                if ((mid_diff <= 0.0) == (lo_diff <= 0.0) && mid_diff * lo_diff > 0) {
                    lo = mid;
                    lo_diff = mid_diff;
                } else {
                    hi = mid;
                }
                last_source = mid_elev.source;
            }
            const double s_hit = 0.5 * (lo + hi);
            result.hit = true;
            result.point = ray_point_enu(s_hit);
            result.geodetic = geodesy::EnuToLla(result.point, in.origin);
            result.elevation_source = last_source;
            result.failure = RaycastFailure::kNone;
            return result;
        }
        prev_s = s;
        prev_diff = diff;
        have_prev = true;
        if (s >= in.march_max_range_m) break;
    }

    result.hit = false;
    result.failure = found_tile ? RaycastFailure::kNoTerrainIntersection : RaycastFailure::kNoDemTile;
    return result;
}

}  // namespace droppoint::core::raycast
