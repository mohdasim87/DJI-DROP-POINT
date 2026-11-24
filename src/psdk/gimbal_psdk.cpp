// psdk/gimbal_psdk.cpp — see header. Status: unverified — requires hardware.
#include "gimbal_psdk.h"

#include "hw/conversions.h"
#include "telemetry_subscription.h"

namespace droppoint::psdk {

PsdkGimbal::PsdkGimbal(PsdkTelemetry& telemetry, util::config::GimbalYawSource yaw_source)
    : telemetry_(telemetry),
      yaw_is_aircraft_(yaw_source == util::config::GimbalYawSource::kEqualsAircraftYaw) {}

core::GimbalState PsdkGimbal::GetAttitude() {
    const auto angles = telemetry_.LatestGimbalAngles();
    if (!angles.valid) {
        core::GimbalState s;
        s.valid = false;
        return s;
    }
    return hw::conv::GimbalAnglesDegToState(angles.pitch_deg, angles.roll_deg, angles.yaw_deg,
                                            yaw_is_aircraft_, telemetry_.AircraftHeadingRad(),
                                            angles.timestamp_us);
}

void PsdkGimbal::SlewRate(double pitch_rate_norm, double yaw_rate_norm) {
    (void)pitch_rate_norm;
    (void)yaw_rate_norm;  // the operator aims with the RC sticks
}

void PsdkGimbal::Reset() {}  // no-op — see header

}  // namespace droppoint::psdk
