// hw/conversions.cpp
#include "conversions.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "core/geodesy.h"
#include "core/math_constants.h"

namespace droppoint::hw::conv {

namespace {

constexpr double kDegToRad = core::kPi / 180.0;
constexpr double kRadToDeg = 180.0 / core::kPi;

}  // namespace

AttitudeRad QuaternionFrdNedToEuler(double w, double x, double y, double z) {
    AttitudeRad out;
    if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        out.heading_rad = out.pitch_rad = out.roll_rad =
            std::numeric_limits<double>::quiet_NaN();
        return out;
    }
    const double norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (norm < 1e-6) {
        out.heading_rad = out.pitch_rad = out.roll_rad =
            std::numeric_limits<double>::quiet_NaN();
        return out;
    }
    w /= norm;
    x /= norm;
    y /= norm;
    z /= norm;

    // Aerospace NED sequence: roll about x, pitch about y, yaw about z.
    out.roll_rad = std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    const double sinp = 2.0 * (w * y - z * x);
    out.pitch_rad = std::asin(std::max(-1.0, std::min(1.0, sinp)));
    out.heading_rad = std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    return out;
}

double EnuYawFromHeadingRad(double heading_rad) {
    return core::geodesy::WrapAngleRad(core::kPi / 2.0 - heading_rad);
}

double RadToDeg(double rad) { return rad * kRadToDeg; }

core::EnuVector NeuToEnu(double x_neu, double y_neu, double z_neu) {
    return core::EnuVector{y_neu, x_neu, z_neu};
}

core::GimbalState GimbalAnglesDegToState(double pitch_deg, double roll_deg, double yaw_deg,
                                        bool yaw_is_aircraft, double aircraft_heading_rad,
                                        std::uint64_t timestamp_us) {
    core::GimbalState s;
    s.timestamp_us = timestamp_us;
    s.pitch_rad = pitch_deg * kDegToRad;   // same convention: 0 = level, down < 0
    s.roll_rad = roll_deg * kDegToRad;
    s.yaw_rad = yaw_is_aircraft ? aircraft_heading_rad : yaw_deg * kDegToRad;
    s.zoom_ratio = 1.0;
    s.valid = std::isfinite(pitch_deg) && std::isfinite(roll_deg) && std::isfinite(yaw_deg);
    return s;
}

hal::RtkFixStatus RtkFixStatusFromProperty(int position_solution_property) {
    switch (position_solution_property) {
        case 50:  // DJI_FC_SUBSCRIPTION_POSITION_SOLUTION_PROPERTY_NARROW_INT
            return hal::RtkFixStatus::kFix;
        case 32:  // FLOAT_L1_AMBIGUITY
        case 33:  // FLOAT_IONOSPHERIC_FREE_AMBIGUITY
        case 34:  // FLOAT_SOLUTION
            return hal::RtkFixStatus::kFloat;
        default:
            return hal::RtkFixStatus::kNotAvailable;
    }
}

namespace {

struct ModelMapEntry {
    int aircraft_type;
    const char* model;
};

// E_DjiAircraftType values, asserted in the Linux adapter against the real
// enum (DJI_AIRCRAFT_TYPE_*).
constexpr ModelMapEntry kModelMap[] = {
    {60, "M300_RTK"},
    {89, "M350_RTK"},
    {67, "M30"},
    {68, "M30T"},
    {77, "M3E"},
    {79, "M3T"},
    {990, "M4E"},
    {99, "M4T"},
    {1000, "M4D"},
    {100, "M4TD"},
    {103, "M400"},
};

}  // namespace

const char* ModelNameFromAircraftType(int aircraft_type) {
    for (const auto& e : kModelMap) {
        if (e.aircraft_type == aircraft_type) return e.model;
    }
    return nullptr;
}

int AircraftTypeForModel(const char* model) {
    if (model == nullptr) return -1;
    for (const auto& e : kModelMap) {
        if (std::strcmp(e.model, model) == 0) return e.aircraft_type;
    }
    return -1;
}

}  // namespace droppoint::hw::conv
