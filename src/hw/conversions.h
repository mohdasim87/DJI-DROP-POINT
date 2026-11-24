// hw/conversions.h
//
// Unit conversions between DJI telemetry representations and DropPoint's
// internal conventions (src/core/solution.h). Deliberately DJI-header-free:
// every function takes primitives so this whole file is unit-testable on
// any platform; the numeric values of DJI enums are pinned by
// static_assert in the Linux-only adapter (src/psdk/telemetry_*).
#pragma once

#include <cstdint>

#include "core/solution.h"
#include "hal/i_gimbal.h"
#include "hal/i_telemetry.h"

namespace droppoint::hw::conv {

struct AttitudeRad {
    double heading_rad = 0.0;  // compass heading: 0 = North, clockwise positive
    double pitch_rad = 0.0;    // positive = nose up
    double roll_rad = 0.0;     // positive = right wing down
};

// Aircraft body FRD -> ground NED quaternion (Hamilton order w, x, y, z) to
// Euler angles. Returns NaNs for a zero/NaN quaternion (caller marks the
// state invalid).
AttitudeRad QuaternionFrdNedToEuler(double w, double x, double y, double z);

// Compass heading (0 = North, CW+) -> the solution.h yaw convention
// (ENU math angle: 0 = East, CCW+), wrapped to (-pi, pi].
double EnuYawFromHeadingRad(double heading_rad);

// Radians -> degrees.
double RadToDeg(double rad);

// NEU velocity (DJI TOPIC_VELOCITY: x = North, y = East, z = Up) -> ENU.
core::EnuVector NeuToEnu(double x_neu, double y_neu, double z_neu);

// GIMBAL_ANGLES sample (degrees, x = pitch, y = roll, z = yaw, ground NED
// frame) -> GimbalState. When `yaw_is_aircraft` (aircraft.yaml
// gimbal_yaw_source: equals_aircraft_yaw) the reported gimbal yaw is
// ignored and `aircraft_heading_rad` (compass convention) is used instead.
// Pitch sign convention is shared: 0 = level, negative = down.
core::GimbalState GimbalAnglesDegToState(double pitch_deg, double roll_deg, double yaw_deg,
                                        bool yaw_is_aircraft, double aircraft_heading_rad,
                                        std::uint64_t timestamp_us);

// RTK_POSITION_INFO (E_DjiFcSubscriptionPositionSolutionProperty) ->
// hal::RtkFixStatus. 50 (NARROW_INT) -> kFix; the float-solution values
// (32/33/34) -> kFloat; everything else -> kNotAvailable.
hal::RtkFixStatus RtkFixStatusFromProperty(int position_solution_property);

// E_DjiAircraftType numeric value -> DropPoint config model name, or
// nullptr when the aircraft is not one this build supports.
const char* ModelNameFromAircraftType(int aircraft_type);

// Inverse lookup: config model name -> E_DjiAircraftType value, or -1.
int AircraftTypeForModel(const char* model);

}  // namespace droppoint::hw::conv
