#include "geodesy.h"
#include "math_constants.h"
#include "test_framework.h"

using namespace droppoint::core;

TEST_CASE("Geodesy: ENU round trip preserves position within 1mm") {
    const LatLonAlt origin_lla{47.6062, -122.3321, 50.0};
    const auto origin = geodesy::MakeEnuOrigin(origin_lla);

    const LatLonAlt point{47.6070, -122.3315, 60.0};
    const EnuPoint enu = geodesy::LlaToEnu(point, origin);
    const LatLonAlt back = geodesy::EnuToLla(enu, origin);

    REQUIRE_NEAR(point.lat_deg, back.lat_deg, 1e-8);
    REQUIRE_NEAR(point.lon_deg, back.lon_deg, 1e-8);
    REQUIRE_NEAR(point.alt_m_hae, back.alt_m_hae, 1e-3);
}

TEST_CASE("Geodesy: origin maps to (0,0,0)") {
    const LatLonAlt origin_lla{10.0, 20.0, 100.0};
    const auto origin = geodesy::MakeEnuOrigin(origin_lla);
    const EnuPoint enu = geodesy::LlaToEnu(origin_lla, origin);
    REQUIRE_NEAR(enu.e, 0.0, 1e-6);
    REQUIRE_NEAR(enu.n, 0.0, 1e-6);
    REQUIRE_NEAR(enu.u, 0.0, 1e-6);
}

TEST_CASE("Geodesy: bearing north is zero, east is 90 degrees") {
    const EnuPoint a{0, 0, 0};
    const EnuPoint north{0, 10, 0};
    const EnuPoint east{10, 0, 0};
    REQUIRE_NEAR(geodesy::BearingRad(a, north), 0.0, 1e-9);
    REQUIRE_NEAR(geodesy::BearingRad(a, east), kPi / 2.0, 1e-9);
}

TEST_CASE("Geodesy: horizontal range is Euclidean in EN plane") {
    const EnuPoint a{0, 0, 5};
    const EnuPoint b{3, 4, 100};
    REQUIRE_NEAR(geodesy::HorizontalRange(a, b), 5.0, 1e-9);
}
