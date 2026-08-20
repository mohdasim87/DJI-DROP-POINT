// core/raycast.h
//
// Inverse projection: pixel (u, v) -> ground point. Implements spec section
// 3.4 steps 1-7 exactly. Shared by Source A (if it ever becomes available,
// see docs/OPEN-QUESTIONS.md #1) and Source C (virtual cursor); Source B
// (gimbal crosshair) is the degenerate case u=c_x, v=c_y.
#pragma once

#include "geodesy.h"
#include "projection.h"
#include "solution.h"
#include "terrain.h"

namespace droppoint::core::raycast {

struct GimbalAttitude {
    double yaw_rad = 0.0;    // resolved per aircraft.yaml gimbal_yaw_source (see spec 3.4)
    double pitch_rad = 0.0;  // 0 = level, negative = looking down
    double roll_rad = 0.0;
};

// Undistorts a normalised (pre-divide, distorted) camera-plane coordinate
// via 5 fixed-point iterations of the inverse Brown-Conrady model. No-op
// when the intrinsics carry no distortion coefficients.
projection::NormalisedPoint Undistort(const projection::NormalisedPoint& distorted,
                                       const projection::Intrinsics& intr);

// Rotates a camera-frame unit vector (x right, y down, z forward) into ENU
// given gimbal attitude, via body-frame (FRD) as the intermediate:
//   d_enu = R_yaw * R_pitch * R_roll * M * d_cam
EnuVector CameraRayToEnu(const projection::CameraPoint& d_cam, const GimbalAttitude& gimbal);

struct RaycastInputs {
    double u_px = 0.0;
    double v_px = 0.0;
    projection::Intrinsics intrinsics;
    double zoom_ratio = 1.0;
    GimbalAttitude gimbal;
    EnuPoint aircraft_position;       // ENU, relative to `origin`
    geodesy::EnuOrigin origin;
    terrain::DemSampleFn dem_fn;
    const terrain::DatumBias* bias = nullptr;  // may be null: raw DEM only
    std::uint64_t now_us = 0;
    double march_step_m = 2.0;
    double march_max_range_m = 500.0;
    int bisection_iterations = 20;
    double horizon_reject_threshold = -0.05;  // spec: reject if d_enu.z >= -0.05
};

// Casts the ray from pixel (u,v) through the current gimbal attitude/zoom
// and intersects it with the terrain (DEM + optional LRF-corrected bias).
RaycastResult CastPixelToGround(const RaycastInputs& in);

// Human-readable name for a RaycastFailure, surfaced in UI/logs.
const char* FailureToString(RaycastFailure failure);

}  // namespace droppoint::core::raycast
