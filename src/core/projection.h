// core/projection.h
//
// Forward projection: a ground point (camera-frame ray) -> pixel. See spec
// section 6.7. This is the mutual inverse of raycast.h and is used both to
// draw markers (predicted impact point, latched target) on the live video
// and by the regression test suite's round-trip check (vector 8).
#pragma once

#include "solution.h"

namespace droppoint::core::projection {

// Camera intrinsics for one (aircraft model, camera index, stream source)
// combination, per lenses.yaml.
struct Intrinsics {
    double image_width_px = 1920.0;
    double image_height_px = 1080.0;
    double hfov_rad = 0.0;   // horizontal field of view at 1x zoom
    double vfov_rad = 0.0;   // vertical field of view at 1x zoom
    double c_x = 960.0;      // principal point, pixels
    double c_y = 540.0;
    // Brown-Conrady distortion; all zero means "no distortion configured",
    // in which case projection/raycast skip the (un)distortion step.
    double k1 = 0.0, k2 = 0.0, k3 = 0.0;
    double p1 = 0.0, p2 = 0.0;
    bool has_distortion = false;
};

// f_x at 1x zoom, derived from HFOV and image width:
//   f_x = (W_px / 2) / tan(HFOV / 2)
double FocalLengthPxX(const Intrinsics& intr);
double FocalLengthPxY(const Intrinsics& intr);

struct ProjectResult {
    bool visible = false;  // false when Zc <= 0 (point behind the camera)
    double u_px = 0.0;
    double v_px = 0.0;
};

// A point in camera frame (x right, y down, z forward), already rotated out
// of ENU by the caller (see raycast.h for the inverse rotation chain).
struct CameraPoint {
    double x = 0.0, y = 0.0, z = 0.0;
};

// Projects a camera-frame point to a pixel, applying live zoom scaling to
// the focal length, per:
//   u = c_x + f_x' * (Xc/Zc)     v = c_y + f_y' * (Yc/Zc)
// Rejects when Zc <= 0.
ProjectResult ProjectToPixel(const CameraPoint& p, const Intrinsics& intr, double zoom_ratio);

// Applies forward Brown-Conrady distortion to a normalised (pre-divide)
// camera-plane coordinate. Used only if the round-trip test wants to
// generate a distorted pixel from an undistorted ray; the guidance loop's
// real distortion correction happens in raycast::Undistort (inverse
// direction). No-op when !intr.has_distortion.
struct NormalisedPoint {
    double x = 0.0, y = 0.0;
};
NormalisedPoint ApplyDistortion(const NormalisedPoint& p, const Intrinsics& intr);

}  // namespace droppoint::core::projection
