#include "projection.h"

#include <cmath>

namespace droppoint::core::projection {

double FocalLengthPxX(const Intrinsics& intr) {
    return (intr.image_width_px / 2.0) / std::tan(intr.hfov_rad / 2.0);
}

double FocalLengthPxY(const Intrinsics& intr) {
    return (intr.image_height_px / 2.0) / std::tan(intr.vfov_rad / 2.0);
}

ProjectResult ProjectToPixel(const CameraPoint& p, const Intrinsics& intr, double zoom_ratio) {
    ProjectResult result;
    if (p.z <= 0.0) {
        result.visible = false;
        return result;
    }
    const double fx = FocalLengthPxX(intr) * zoom_ratio;
    const double fy = FocalLengthPxY(intr) * zoom_ratio;

    double x = p.x / p.z;
    double y = p.y / p.z;
    if (intr.has_distortion) {
        const NormalisedPoint distorted = ApplyDistortion(NormalisedPoint{x, y}, intr);
        x = distorted.x;
        y = distorted.y;
    }

    result.u_px = intr.c_x + fx * x;
    result.v_px = intr.c_y + fy * y;
    result.visible = true;
    return result;
}

NormalisedPoint ApplyDistortion(const NormalisedPoint& p, const Intrinsics& intr) {
    if (!intr.has_distortion) return p;
    const double r2 = p.x * p.x + p.y * p.y;
    const double r4 = r2 * r2;
    const double r6 = r4 * r2;
    const double radial = 1.0 + intr.k1 * r2 + intr.k2 * r4 + intr.k3 * r6;
    const double dx = 2.0 * intr.p1 * p.x * p.y + intr.p2 * (r2 + 2.0 * p.x * p.x);
    const double dy = intr.p1 * (r2 + 2.0 * p.y * p.y) + 2.0 * intr.p2 * p.x * p.y;
    NormalisedPoint out;
    out.x = p.x * radial + dx;
    out.y = p.y * radial + dy;
    return out;
}

}  // namespace droppoint::core::projection
