// psdk/gimbal_psdk.cpp
//
// Gimbal control and attitude readback via DJI PSDK GimbalManager.
// Status: unverified — compiles against real headers, needs hardware to verify.
#include "gimbal_psdk.h"

#include "psdk_util.h"
#include "util/logging.h"

#include "dji_gimbal_manager.h"
#include "dji_typedef.h"

namespace droppoint::psdk {

namespace {

// The camera/crosshair gimbal is assumed to be mounted at the aircraft's
// primary payload port position. If your aircraft has multiple gimbals, change
// this in config/aircraft.yaml or make it configurable.
constexpr E_DjiMountPosition kGimbalMountPosition = DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1;

}  // namespace

GimbalPsdk::GimbalPsdk(util::config::GimbalYawSource yaw_source) : yaw_source_(yaw_source) {
    CheckReturn(DjiGimbalManager_Init(), "DjiGimbalManager_Init");
    CheckReturn(DjiGimbalManager_SetMode(kGimbalMountPosition, DJI_GIMBAL_MODE_YAW_FOLLOW),
                "DjiGimbalManager_SetMode");
}

GimbalPsdk::~GimbalPsdk() {
    DjiGimbalManager_Deinit();
}

void GimbalPsdk::UpdateAngles(const T_DjiVector3f& angles_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = GimbalAnglesToEnu(angles_deg);
    latest_.valid = true;
    timestamp_us_ = MonotonicUs();

    // If the gimbal yaw is defined to equal aircraft yaw, the raycast solver
    // should ignore the gimbal yaw topic and use aircraft yaw instead. We
    // signal that by clearing the yaw value here; main_psdk.cpp wires the
    // aircraft yaw into raycast via the gimbal_yaw_source config field.
    if (yaw_source_ == util::config::GimbalYawSource::kEqualsAircraftYaw) {
        latest_.yaw_rad = 0.0;
    }
}

core::GimbalState GimbalPsdk::GetAttitude() {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

void GimbalPsdk::SlewRate(double pitch_rate_norm, double yaw_rate_norm) {
    // Normalise to the range the gimbal manager expects. The exact maximum
    // rate depends on the aircraft/gimbal; here we use a conservative scaling
    // of +/-30 deg/s and rely on operator testing to adjust.
    T_DjiGimbalManagerRotation rotation{};
    rotation.rotationMode = DJI_GIMBAL_ROTATION_MODE_SPEED;
    rotation.pitch = static_cast<dji_f32_t>(pitch_rate_norm * 30.0);
    rotation.yaw = static_cast<dji_f32_t>(yaw_rate_norm * 30.0);
    rotation.roll = 0.0f;
    rotation.time = 0.5;

    const T_DjiReturnCode code = DjiGimbalManager_Rotate(kGimbalMountPosition, rotation);
    if (code != DJI_RETURN_CODE_OK) {
        DP_LOG_WARN("gimbal_psdk",
                    "DjiGimbalManager_Rotate failed: 0x" + std::to_string(code));
    }
}

void GimbalPsdk::Reset() {
    DjiGimbalManager_Reset(kGimbalMountPosition, DJI_GIMBAL_RESET_MODE_PITCH_AND_YAW);
}

}  // namespace droppoint::psdk
