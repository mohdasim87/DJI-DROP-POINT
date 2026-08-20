// psdk/telemetry_subscription.h
//
// Real aircraft telemetry via DJI PSDK Data Subscription topics.
// Status: unverified — compiles against real headers, needs hardware to verify.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "core/geodesy.h"
#include "core/solution.h"
#include "hal/i_telemetry.h"
#include "util/config.h"

// Real PSDK headers (allowed only in src/psdk).
#include "dji_fc_subscription.h"
#include "dji_typedef.h"

namespace droppoint::psdk {

// Origin shared between telemetry, target sources, and the guidance loop so
// all ENU coordinates are in the same tangent plane.
using SharedEnuOrigin = std::shared_ptr<core::geodesy::EnuOrigin>;

class TelemetrySubscription : public hal::ITelemetry {
public:
    TelemetrySubscription(const util::config::AircraftConfig& aircraft_cfg,
                          const util::config::SystemConfig& system_cfg,
                          SharedEnuOrigin origin);
    ~TelemetrySubscription() override;

    // Access to the ENU origin (useful for main to share with other modules).
    SharedEnuOrigin Origin() const { return origin_; }

    // ITelemetry implementation.
    core::AircraftState GetAircraftState() override;
    hal::GnssStatus GetGnssStatus() override;
    double GetBarometricAltitudeM() override;
    std::uint64_t GetTelemetryAgeUs() override;
    core::WindSample GetWind() override;
    std::vector<hal::RcButtonEvent> PollRcButtonEvents() override;
    void RegisterUpdateCallback(UpdateCallback callback) override;

    // Callbacks called from the PSDK subscription threads. Public only because
    // the C callback signature has no user-data pointer; do not call directly.
    void OnQuaternion(const T_DjiFcSubscriptionQuaternion& q, std::uint64_t now_us);
    void OnVelocity(const T_DjiFcSubscriptionVelocity& v, std::uint64_t now_us);
    void OnPositionFused(const T_DjiFcSubscriptionPositionFused& p, std::uint64_t now_us);
    void OnAltitudeFused(const float altitude_m, std::uint64_t now_us);
    void OnAccelerationGround(const T_DjiVector3f& a, std::uint64_t now_us);
    void OnGpsDetails(const T_DjiFcSubscriptionGpsDetails& d, std::uint64_t now_us);
    void OnRtkPosition(const T_DjiFcSubscriptionRtkPosition& p, std::uint64_t now_us);
    void OnRtkYaw(const std::int16_t yaw_cdeg, std::uint64_t now_us);
    void OnGimbalAngles(const T_DjiVector3f& angles, std::uint64_t now_us);
    void OnRc(const T_DjiFcSubscriptionRC& rc, std::uint64_t now_us);

private:
    util::config::AircraftConfig aircraft_cfg_;
    util::config::SystemConfig system_cfg_;
    SharedEnuOrigin origin_;

    mutable std::mutex mutex_;

    // Latest timestamp of any topic update (monotonic us).
    std::uint64_t last_update_us_ = 0;

    // Aircraft state pieces, protected by mutex_.
    core::LatLonAlt geodetic_{};
    core::EnuVector velocity_{};
    T_DjiFcSubscriptionQuaternion quat_{};
    bool have_quat_ = false;
    float altitude_fused_m_ = 0.0f;
    bool have_altitude_ = false;
    T_DjiFcSubscriptionGpsDetails gps_details_{};
    bool have_gps_details_ = false;
    bool rtk_fix_ = false;
    T_DjiFcSubscriptionRtkPosition rtk_position_{};
    bool have_rtk_position_ = false;
    bool have_rtk_yaw_ = false;
    T_DjiVector3f acceleration_ground_{};
    bool have_acceleration_ = false;

    T_DjiFcSubscriptionRC last_rc_{};
    bool have_rc_ = false;

    std::vector<UpdateCallback> callbacks_;

    void UpdateOriginLocked(const core::LatLonAlt& lla);
    void NotifyCallbacksLocked(const core::AircraftState& state);
    core::AircraftState BuildAircraftStateLocked() const;
};

}  // namespace droppoint::psdk
