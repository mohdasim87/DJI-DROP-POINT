// psdk/gimbal_psdk.h
//
// Gimbal control and attitude readback via DJI PSDK GimbalManager and the
// FC-subscription gimbal-angles topic. Status: unverified.
#pragma once

#include <mutex>

#include "core/solution.h"
#include "hal/i_gimbal.h"
#include "util/config.h"

#include "dji_typedef.h"

namespace droppoint::psdk {

// Consumes the latest gimbal angles, which are produced by the PSDK
// telemetry subscription callback. The telemetry adapter is the only place
// that can subscribe to DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES.
class GimbalPsdk : public hal::IGimbal {
public:
    explicit GimbalPsdk(util::config::GimbalYawSource yaw_source);
    ~GimbalPsdk() override;

    // Called by TelemetrySubscription whenever fresh gimbal angles arrive.
    void UpdateAngles(const T_DjiVector3f& angles_deg);

    // IGimbal implementation.
    core::GimbalState GetAttitude() override;
    void SlewRate(double pitch_rate_norm, double yaw_rate_norm) override;
    void Reset() override;

private:
    util::config::GimbalYawSource yaw_source_;
    std::mutex mutex_;
    core::GimbalState latest_{};
    std::uint64_t timestamp_us_ = 0;
};

}  // namespace droppoint::psdk
