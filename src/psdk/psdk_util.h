// psdk/psdk_util.h
//
// Internal helpers for the PSDK adapter layer: coordinate/attitude
// conversions, return-code checking, and small utilities. This header is not
// part of the public HAL interface.
#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "core/geodesy.h"
#include "core/math_constants.h"
#include "core/solution.h"
#include "util/logging.h"

// DJI Payload-SDK headers. These are the only DJI includes outside the
// individual adapter .cpp files, and they are used solely to convert PSDK C
// types into DropPoint core types.
#include "dji_error.h"
#include "dji_fc_subscription.h"
#include "dji_typedef.h"

namespace droppoint::psdk {

constexpr double kDegToRad = core::kPi / 180.0;
constexpr double kRadToDeg = 180.0 / core::kPi;
constexpr double kTenthDegToRad = core::kPi / 1800.0;  // 0.1 deg -> rad

// Throwing wrapper. PSDK functions return T_DjiReturnCode; non-success is
// turned into a std::runtime_error so constructors can fail fast.
inline void CheckReturn(T_DjiReturnCode code, const std::string& context) {
    if (code == DJI_RETURN_CODE_OK) return;
    throw std::runtime_error(context + " failed: PSDK return code 0x" + std::to_string(code));
}

// Converts a PSDK quaternion (body FRD -> ground NED, Hamilton convention,
// q0 = w) into DropPoint ENU aircraft Euler angles using the Z-Y-X
// (Tait-Bryan) sequence expected by core::raycast and core::wind.
//
// Status: unverified — needs real-flight data to confirm sign/gimbal-lock
// behaviour. The conversion is implemented exactly so the code compiles
// against the real PSDK headers.
inline core::AircraftState QuaternionToEnu(const T_DjiFcSubscriptionQuaternion& q,
                                            const core::geodesy::EnuOrigin& origin) {
    core::AircraftState state{};

    // Normalise the quaternion (defensive: PSDK data may not be exactly
    // unit-length in real telemetry).
    const double norm = std::sqrt(static_cast<double>(q.q0) * q.q0 +
                                  static_cast<double>(q.q1) * q.q1 +
                                  static_cast<double>(q.q2) * q.q2 +
                                  static_cast<double>(q.q3) * q.q3);
    if (norm < 1e-12) return state;  // invalid quaternion

    const double w = static_cast<double>(q.q0) / norm;
    const double x = static_cast<double>(q.q1) / norm;
    const double y = static_cast<double>(q.q2) / norm;
    const double z = static_cast<double>(q.q3) / norm;

    // Build the body-FRD -> NED rotation matrix from the quaternion.
    const double r11 = 1.0 - 2.0 * (y * y + z * z);
    const double r12 = 2.0 * (x * y - w * z);
    const double r13 = 2.0 * (x * z + w * y);
    const double r21 = 2.0 * (x * y + w * z);
    const double r22 = 1.0 - 2.0 * (x * x + z * z);
    const double r23 = 2.0 * (y * z - w * x);
    const double r31 = 2.0 * (x * z - w * y);
    const double r32 = 2.0 * (y * z + w * x);
    const double r33 = 1.0 - 2.0 * (x * x + y * y);

    // Convert NED -> ENU. The mapping is:
    //   ENU.x =  NED.y   (East)
    //   ENU.y =  NED.x   (North)
    //   ENU.z = -NED.z   (Up)
    // Applying the same similarity transform to the rotation matrix gives the
    // body-FRD -> ENU matrix.
    const double e11 = r22;
    const double e12 = r21;
    const double e13 = -r23;
    const double e21 = r12;
    const double e22 = r11;
    const double e23 = -r13;
    const double e31 = -r32;
    const double e32 = -r31;
    const double e33 = r33;

    // Extract Z-Y-X Euler angles from the ENU rotation matrix.
    state.pitch_rad = std::asin(std::max(-1.0, std::min(1.0, -e31)));
    state.roll_rad = std::atan2(e32, e33);
    state.yaw_rad = std::atan2(e21, e11);

    state.position = core::geodesy::LlaToEnu(state.geodetic, origin);
    state.valid = true;
    return state;
}

// Converts PSDK gimbal angles (Vector3f in degrees, x=pitch, y=roll, z=yaw,
// NED frame) into DropPoint ENU Euler angles for raycast.
inline core::GimbalState GimbalAnglesToEnu(const T_DjiVector3f& angles_deg) {
    core::GimbalState state{};

    const double pitch_ned = static_cast<double>(angles_deg.x) * kDegToRad;
    const double roll_ned = static_cast<double>(angles_deg.y) * kDegToRad;
    const double yaw_ned = static_cast<double>(angles_deg.z) * kDegToRad;

    // Build the gimbal attitude rotation matrix in NED using the same Z-Y-X
    // sequence as the aircraft, then convert to ENU.
    const double cz = std::cos(yaw_ned);
    const double sz = std::sin(yaw_ned);
    const double cy = std::cos(pitch_ned);
    const double sy = std::sin(pitch_ned);
    const double cx = std::cos(roll_ned);
    const double sx = std::sin(roll_ned);

    const double r11 = cz * cy;
    const double r12 = cz * sy * sx - sz * cx;
    const double r13 = cz * sy * cx + sz * sx;
    const double r21 = sz * cy;
    const double r22 = sz * sy * sx + cz * cx;
    const double r23 = sz * sy * cx - cz * sx;
    const double r31 = -sy;
    const double r32 = cy * sx;
    const double r33 = cy * cx;

    // NED -> ENU similarity transform.
    const double e11 = r22;
    const double e12 = r21;
    const double e13 = -r23;
    const double e21 = r12;
    const double e22 = r11;
    const double e23 = -r13;
    const double e31 = -r32;
    const double e32 = -r31;
    const double e33 = r33;

    state.pitch_rad = std::asin(std::max(-1.0, std::min(1.0, -e31)));
    state.roll_rad = std::atan2(e32, e33);
    state.yaw_rad = std::atan2(e21, e11);
    state.valid = true;

    return state;
}

// Converts a PSDK NEU velocity (x=North, y=East, z=Up) into ENU.
inline core::EnuVector NeuToEnu(const T_DjiVector3f& v) {
    core::EnuVector out{};
    out.n = static_cast<double>(v.x);
    out.e = static_cast<double>(v.y);
    out.u = static_cast<double>(v.z);
    return out;
}

// Current monotonic time in microseconds, using the same clock the platform
// OSAL will register (CLOCK_MONOTONIC on Linux).
std::uint64_t MonotonicUs();

}  // namespace droppoint::psdk
