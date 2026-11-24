// tests/test_hw_conversions.cpp
//
// Unit coverage for hw/conversions.*: quaternion/velocity/gimbal
// conversions, RTK fix mapping and the aircraft-type <-> model table.
// Numeric DJI enum values used here are pinned by static_assert in the
// Linux adapter (src/psdk).
#include <cmath>

#include "test_framework.h"

#include "core/math_constants.h"
#include "core/wind.h"
#include "hw/conversions.h"

namespace {

constexpr double kDeg = droppoint::core::kPi / 180.0;

// Hamilton product a*b for unit quaternions (w, x, y, z).
struct Quat {
    double w, x, y, z;
};
Quat Mul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Quat AxisZ(double angle) { return {std::cos(angle / 2), 0, 0, std::sin(angle / 2)}; }
Quat AxisY(double angle) { return {std::cos(angle / 2), 0, std::sin(angle / 2), 0}; }
Quat AxisX(double angle) { return {std::cos(angle / 2), std::sin(angle / 2), 0, 0}; }

}  // namespace

TEST_CASE("Conv: identity quaternion gives zero attitude and ENU yaw pi/2") {
    const auto att = droppoint::hw::conv::QuaternionFrdNedToEuler(1.0, 0.0, 0.0, 0.0);
    REQUIRE_NEAR(att.heading_rad, 0.0, 1e-9);
    REQUIRE_NEAR(att.pitch_rad, 0.0, 1e-9);
    REQUIRE_NEAR(att.roll_rad, 0.0, 1e-9);
    REQUIRE_NEAR(droppoint::hw::conv::EnuYawFromHeadingRad(att.heading_rad),
                 droppoint::core::kPi / 2.0, 1e-9);
}

TEST_CASE("Conv: 90 deg heading quaternion gives ENU yaw ~0") {
    const Quat q = AxisZ(90.0 * kDeg);  // NED yaw +90 = compass heading east
    const auto att = droppoint::hw::conv::QuaternionFrdNedToEuler(q.w, q.x, q.y, q.z);
    REQUIRE_NEAR(att.heading_rad, droppoint::core::kPi / 2.0, 1e-6);
    REQUIRE_NEAR(att.pitch_rad, 0.0, 1e-6);
    REQUIRE_NEAR(droppoint::hw::conv::EnuYawFromHeadingRad(att.heading_rad), 0.0, 1e-6);
}

TEST_CASE("Conv: 10 deg nose down and 10 deg right roll") {
    const Quat qd = AxisY(-10.0 * kDeg);
    auto att = droppoint::hw::conv::QuaternionFrdNedToEuler(qd.w, qd.x, qd.y, qd.z);
    REQUIRE_NEAR(att.pitch_rad, -10.0 * kDeg, 1e-6);
    REQUIRE_NEAR(att.roll_rad, 0.0, 1e-6);

    const Quat qr = AxisX(10.0 * kDeg);
    att = droppoint::hw::conv::QuaternionFrdNedToEuler(qr.w, qr.x, qr.y, qr.z);
    REQUIRE_NEAR(att.roll_rad, 10.0 * kDeg, 1e-6);
    REQUIRE_NEAR(att.pitch_rad, 0.0, 1e-6);
}

TEST_CASE("Conv: combined attitude feeds the hover wind estimator correctly") {
    // Heading north (identity yaw) with +roll (lean right = lean east) is
    // the wind-from-east case pinned by test_wind.cpp.
    const Quat q = AxisX(0.05);
    const auto att = droppoint::hw::conv::QuaternionFrdNedToEuler(q.w, q.x, q.y, q.z);
    droppoint::core::wind::HoverEstimateInputs in;
    in.pitch_rad = att.pitch_rad;
    in.roll_rad = att.roll_rad;
    in.yaw_rad = droppoint::hw::conv::EnuYawFromHeadingRad(att.heading_rad);
    in.mass_kg = 4.0;
    in.cda_airframe = 0.05;
    const auto sample = droppoint::core::wind::EstimateFromHover(in);
    REQUIRE(sample.valid);
    REQUIRE_NEAR(sample.from_bearing_rad, droppoint::core::kPi / 2.0, 1e-3);
}

TEST_CASE("Conv: NaN and zero quaternions are invalid") {
    auto att = droppoint::hw::conv::QuaternionFrdNedToEuler(0.0, 0.0, 0.0, 0.0);
    REQUIRE(std::isnan(att.heading_rad));
    att = droppoint::hw::conv::QuaternionFrdNedToEuler(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0);
    REQUIRE(std::isnan(att.pitch_rad));
}

TEST_CASE("Conv: radians to degrees and NEU velocity to ENU") {
    REQUIRE_NEAR(droppoint::hw::conv::RadToDeg(0.5934), 34.0, 1e-3);
    const auto v = droppoint::hw::conv::NeuToEnu(1.0, 2.0, 3.0);
    REQUIRE_NEAR(v.e, 2.0, 1e-12);
    REQUIRE_NEAR(v.n, 1.0, 1e-12);
    REQUIRE_NEAR(v.u, 3.0, 1e-12);
}

TEST_CASE("Conv: gimbal yaw selection, both yaw-source modes") {
    using droppoint::hw::conv::GimbalAnglesDegToState;
    const auto indep = GimbalAnglesDegToState(-45.0, 3.0, 120.0, false, 0.0, 0);
    REQUIRE(indep.valid);
    REQUIRE_NEAR(indep.yaw_rad, 120.0 * kDeg, 1e-9);
    REQUIRE_NEAR(indep.pitch_rad, -45.0 * kDeg, 1e-9);
    REQUIRE_NEAR(indep.roll_rad, 3.0 * kDeg, 1e-9);
    REQUIRE_NEAR(indep.zoom_ratio, 1.0, 1e-12);

    const double aircraft_heading = 30.0 * kDeg;
    const auto tied = GimbalAnglesDegToState(-45.0, 3.0, 120.0, true, aircraft_heading, 0);
    REQUIRE_NEAR(tied.yaw_rad, aircraft_heading, 1e-12);
}

TEST_CASE("Conv: RTK property mapping") {
    using droppoint::hal::RtkFixStatus;
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(50) == RtkFixStatus::kFix);
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(32) == RtkFixStatus::kFloat);
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(33) == RtkFixStatus::kFloat);
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(34) == RtkFixStatus::kFloat);
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(48) == RtkFixStatus::kNotAvailable);
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(16) == RtkFixStatus::kNotAvailable);
    REQUIRE(droppoint::hw::conv::RtkFixStatusFromProperty(0) == RtkFixStatus::kNotAvailable);
}

TEST_CASE("Conv: aircraft type <-> model name table") {
    using droppoint::hw::conv::AircraftTypeForModel;
    using droppoint::hw::conv::ModelNameFromAircraftType;
    REQUIRE(std::string(ModelNameFromAircraftType(99)) == "M4T");
    REQUIRE(std::string(ModelNameFromAircraftType(103)) == "M400");
    REQUIRE(std::string(ModelNameFromAircraftType(60)) == "M300_RTK");
    REQUIRE(std::string(ModelNameFromAircraftType(89)) == "M350_RTK");
    REQUIRE(std::string(ModelNameFromAircraftType(68)) == "M30T");
    REQUIRE(std::string(ModelNameFromAircraftType(79)) == "M3T");
    REQUIRE(std::string(ModelNameFromAircraftType(990)) == "M4E");
    REQUIRE(std::string(ModelNameFromAircraftType(100)) == "M4TD");
    REQUIRE(std::string(ModelNameFromAircraftType(1000)) == "M4D");
    REQUIRE(std::string(ModelNameFromAircraftType(67)) == "M30");
    REQUIRE(std::string(ModelNameFromAircraftType(77)) == "M3E");
    REQUIRE(ModelNameFromAircraftType(91) == nullptr);  // M3D not supported
    REQUIRE(AircraftTypeForModel("M4T") == 99);
    REQUIRE(AircraftTypeForModel("M400") == 103);
    REQUIRE(AircraftTypeForModel("NOPE") == -1);
}
