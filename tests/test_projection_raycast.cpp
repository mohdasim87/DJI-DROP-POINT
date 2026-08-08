#include <cmath>
#include <optional>

#include "geodesy.h"
#include "math_constants.h"
#include "projection.h"
#include "raycast.h"
#include "terrain.h"
#include "test_framework.h"

using namespace droppoint::core;

namespace {

projection::Intrinsics MakeIntrinsics() {
    projection::Intrinsics intr;
    intr.image_width_px = 1920.0;
    intr.image_height_px = 1080.0;
    intr.hfov_rad = 60.0 * kPi / 180.0;
    intr.vfov_rad = 35.0 * kPi / 180.0;
    intr.c_x = 960.0;
    intr.c_y = 540.0;
    intr.has_distortion = false;
    return intr;
}

// Flat terrain at absolute elevation 0, matching an ENU origin at alt 0.
terrain::DemSampleFn FlatDem(double elevation_m = 0.0) {
    return [elevation_m](double, double) -> std::optional<double> { return elevation_m; };
}

geodesy::EnuOrigin MakeOrigin() {
    return geodesy::MakeEnuOrigin(LatLonAlt{47.6062, -122.3321, 0.0});
}

}  // namespace

// Regression vector 7: nadir gimbal, centre pixel, flat terrain at 50 m ->
// ray hits directly below the aircraft, within 1 cm.
TEST_CASE("Regression 7: nadir gimbal centre pixel hits directly below") {
    raycast::RaycastInputs in;
    in.u_px = 960.0;
    in.v_px = 540.0;
    in.intrinsics = MakeIntrinsics();
    in.zoom_ratio = 1.0;
    in.gimbal = raycast::GimbalAttitude{0.0, -kPi / 2.0, 0.0};  // straight down
    in.aircraft_position = EnuPoint{0.0, 0.0, 50.0};
    in.origin = MakeOrigin();
    in.dem_fn = FlatDem(0.0);
    in.bias = nullptr;
    in.now_us = 0;

    const RaycastResult result = raycast::CastPixelToGround(in);
    REQUIRE(result.hit);
    REQUIRE_NEAR(result.point.e, 0.0, 0.01);
    REQUIRE_NEAR(result.point.n, 0.0, 0.01);
    REQUIRE_NEAR(result.point.u, 0.0, 0.01);
}

// Regression vector 8: round trip. Project a ground point to a pixel using
// the same rotation raycast uses (verified via orthonormal basis vectors),
// then raycast that pixel back; agreement within 1 cm.
TEST_CASE("Regression 8: projection and raycast are mutual inverses") {
    const raycast::GimbalAttitude gimbal{20.0 * kPi / 180.0, -45.0 * kPi / 180.0, 5.0 * kPi / 180.0};
    const EnuPoint aircraft{0.0, 0.0, 60.0};
    const EnuPoint target{18.0, -9.0, 0.0};  // on the flat DEM plane (elevation 0)

    // Orthonormal camera-axis basis vectors expressed in ENU, from the same
    // rotation raycast::CameraRayToEnu uses.
    const EnuVector basis_x = raycast::CameraRayToEnu(projection::CameraPoint{1, 0, 0}, gimbal);
    const EnuVector basis_y = raycast::CameraRayToEnu(projection::CameraPoint{0, 1, 0}, gimbal);
    const EnuVector basis_z = raycast::CameraRayToEnu(projection::CameraPoint{0, 0, 1}, gimbal);

    const EnuVector dir{target.e - aircraft.e, target.n - aircraft.n, target.u - aircraft.u};
    const double cx = dir.e * basis_x.e + dir.n * basis_x.n + dir.u * basis_x.u;
    const double cy = dir.e * basis_y.e + dir.n * basis_y.n + dir.u * basis_y.u;
    const double cz = dir.e * basis_z.e + dir.n * basis_z.n + dir.u * basis_z.u;
    REQUIRE(cz > 0.0);  // target must be in front of the camera

    const projection::Intrinsics intr = MakeIntrinsics();
    const projection::ProjectResult proj =
        projection::ProjectToPixel(projection::CameraPoint{cx, cy, cz}, intr, 1.0);
    REQUIRE(proj.visible);

    raycast::RaycastInputs in;
    in.u_px = proj.u_px;
    in.v_px = proj.v_px;
    in.intrinsics = intr;
    in.zoom_ratio = 1.0;
    in.gimbal = gimbal;
    in.aircraft_position = aircraft;
    in.origin = MakeOrigin();
    in.dem_fn = FlatDem(0.0);

    const RaycastResult result = raycast::CastPixelToGround(in);
    REQUIRE(result.hit);
    REQUIRE_NEAR(result.point.e, target.e, 0.01);
    REQUIRE_NEAR(result.point.n, target.n, 0.01);
}

// Regression vector 9: same pixel at 1x and 2x must yield different ground
// points; failing to scale focal length is the bug this catches.
TEST_CASE("Regression 9: zoom scaling changes the resolved ground point") {
    raycast::RaycastInputs in;
    in.u_px = 1200.0;  // off-centre pixel so zoom actually matters
    in.v_px = 700.0;
    in.intrinsics = MakeIntrinsics();
    in.gimbal = raycast::GimbalAttitude{0.0, -70.0 * kPi / 180.0, 0.0};
    in.aircraft_position = EnuPoint{0.0, 0.0, 60.0};
    in.origin = MakeOrigin();
    in.dem_fn = FlatDem(0.0);

    in.zoom_ratio = 1.0;
    const RaycastResult r1 = raycast::CastPixelToGround(in);
    in.zoom_ratio = 2.0;
    const RaycastResult r2 = raycast::CastPixelToGround(in);

    REQUIRE(r1.hit);
    REQUIRE(r2.hit);
    const double d = geodesy::HorizontalRange(r1.point, r2.point);
    REQUIRE(d > 0.5);  // must differ meaningfully, not just numerical noise
}

// Regression vector 10: horizon rejection. Upward or level ray returns
// no-target with a reason.
TEST_CASE("Regression 10: level ray is rejected as horizon/sky") {
    raycast::RaycastInputs in;
    in.u_px = 960.0;
    in.v_px = 540.0;  // centre pixel, gimbal level -> ray is horizontal
    in.intrinsics = MakeIntrinsics();
    in.gimbal = raycast::GimbalAttitude{0.0, 0.0, 0.0};
    in.aircraft_position = EnuPoint{0.0, 0.0, 60.0};
    in.origin = MakeOrigin();
    in.dem_fn = FlatDem(0.0);

    const RaycastResult result = raycast::CastPixelToGround(in);
    REQUIRE(!result.hit);
    REQUIRE(result.failure == RaycastFailure::kRayAboveHorizon);
}

TEST_CASE("Raycast: no DEM tile reports a distinct failure reason") {
    raycast::RaycastInputs in;
    in.u_px = 960.0;
    in.v_px = 540.0;
    in.intrinsics = MakeIntrinsics();
    in.gimbal = raycast::GimbalAttitude{0.0, -kPi / 2.0, 0.0};
    in.aircraft_position = EnuPoint{0.0, 0.0, 50.0};
    in.origin = MakeOrigin();
    in.dem_fn = [](double, double) -> std::optional<double> { return std::nullopt; };

    const RaycastResult result = raycast::CastPixelToGround(in);
    REQUIRE(!result.hit);
    REQUIRE(result.failure == RaycastFailure::kNoDemTile);
}
