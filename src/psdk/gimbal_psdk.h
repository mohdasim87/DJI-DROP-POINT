// psdk/gimbal_psdk.h
//
// Status: unverified — requires hardware.
//
// hal::IGimbal backed by the GIMBAL_ANGLES subscription topic polled by
// PsdkTelemetry (basic subscription is the only camera-adjacent data a
// third-party companion gets on the M4T and M400 — Gimbal Manager is an
// advanced feature, see docs/CZI-DROP-KITS.md). SlewRate()/Reset() are
// no-ops: the operator aims the camera with the RC, exactly as in the sim.
#pragma once

#include "hal/i_gimbal.h"
#include "util/config.h"

namespace droppoint::psdk {

class PsdkTelemetry;

class PsdkGimbal : public hal::IGimbal {
public:
    PsdkGimbal(PsdkTelemetry& telemetry, util::config::GimbalYawSource yaw_source);

    core::GimbalState GetAttitude() override;
    void SlewRate(double pitch_rate_norm, double yaw_rate_norm) override;
    void Reset() override;

private:
    PsdkTelemetry& telemetry_;
    bool yaw_is_aircraft_;
};

}  // namespace droppoint::psdk
