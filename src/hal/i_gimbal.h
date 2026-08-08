// hal/i_gimbal.h
//
// Gimbal attitude readback and slew commands, for Source B (crosshair) and
// the raycast solver's attitude input. Yaw *source* (independent gimbal yaw
// vs. aircraft-yaw-equals-gimbal-yaw) is resolved by the implementation per
// aircraft.yaml, per spec 3.4 — this interface always returns a fully
// resolved yaw regardless of which physical path produced it.
#pragma once

#include "core/solution.h"

namespace droppoint::hal {

class IGimbal {
public:
    virtual ~IGimbal() = default;

    virtual core::GimbalState GetAttitude() = 0;

    // Slews the gimbal at the given normalised rates (-1..1) for crosshair
    // control (Source B). No-op / clamped safely if the gimbal cannot slew
    // independently (aircraft where gimbal yaw == aircraft yaw).
    virtual void SlewRate(double pitch_rate_norm, double yaw_rate_norm) = 0;

    virtual void Reset() = 0;  // recentre to a known attitude
};

}  // namespace droppoint::hal
