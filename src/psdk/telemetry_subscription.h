// psdk/telemetry_subscription.h
//
// Status: unverified — requires hardware.
//
// hal::ITelemetry backed by the PSDK fc-subscription module. All topics are
// subscribed with a NULL callback and polled inside GetAircraftState() /
// PollExtras() via DjiFcSubscription_GetLatestValueOfTopic — no PSDK worker
// thread ever touches our state, so nothing here needs a lock beyond the
// poll itself.
//
// Topic plan (docs/CZI-DROP-KITS.md capability matrix — basic subscription
// is all a third-party companion gets on both airframes):
//   required: QUATERNION, VELOCITY, POSITION_FUSED, GIMBAL_ANGLES @50 Hz
//             (a required topic that rejects 50 Hz is retried at 10 Hz)
//   optional: STATUS_FLIGHT, RTK_POSITION_INFO, ALTITUDE_OF_HOMEPOINT,
//             ALTITUDE_BAROMETER @10 Hz (failure is logged and surfaced in
//             the self-test report, never fatal)
//
// Stale-data rule: AircraftState.timestamp_us is the LOCAL monotonic clock
// at the moment the DJI timestamp of the position or quaternion sample
// last advanced. A frozen DJI timestamp means the link stopped delivering
// new data, so GetTelemetryAgeUs() grows — the guidance loop's stale gate
// then aborts the countdown by itself.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/geodesy.h"
#include "core/solution.h"
#include "hal/i_telemetry.h"

namespace droppoint::psdk {

class PsdkTelemetry : public hal::ITelemetry {
public:
    PsdkTelemetry() = default;
    ~PsdkTelemetry() override = default;

    // Calls DjiFcSubscription_Init() and subscribes every topic above.
    // Throws std::runtime_error when a required topic cannot be subscribed
    // at any frequency. Optional failures land in TopicStatus().
    void Init();

    struct TopicStatusEntry {
        std::string name;
        bool subscribed = false;
        int frequency_hz = 0;
        bool required = false;
    };
    // Per-topic outcome for the self-test report.
    std::vector<TopicStatusEntry> TopicStatus() const;

    // Polls every subscribed topic once (called implicitly by
    // GetAircraftState; also call it from the tick even when the loop does
    // not need aircraft state, so the staleness clock stays honest).
    void Poll();

    // hal::ITelemetry
    core::AircraftState GetAircraftState() override;
    hal::GnssStatus GetGnssStatus() override;
    double GetBarometricAltitudeM() override;
    std::uint64_t GetTelemetryAgeUs() override;
    core::WindSample GetWind() override;  // always invalid — no PSDK wind topic
    std::vector<hal::RcButtonEvent> PollRcButtonEvents() override;  // empty — widgets only
    void RegisterUpdateCallback(UpdateCallback callback) override;

    // ENU origin: the first POSITION_FUSED sample with >= 6 visible
    // satellites. Set automatically inside Poll(); the self-test reads it.
    bool HasEnuOrigin() const;
    core::LatLonAlt EnuOriginLla() const;

    // Extras for the gimbal adapter / main loop / home-tie logic.
    struct GimbalAngles {
        double pitch_deg = 0.0;
        double roll_deg = 0.0;
        double yaw_deg = 0.0;
        std::uint64_t timestamp_us = 0;
        bool valid = false;
    };
    GimbalAngles LatestGimbalAngles() const;
    // 0 = stopped, 1 = on ground, 2 = in air (E_DjiFcSubscriptionFlightStatus);
    // nullopt when the optional topic failed to subscribe.
    std::optional<int> LatestFlightStatus() const;
    std::optional<double> LatestHomeAltitudeM() const;
    // Local-monotonic timestamp of the last attitude sample (for gimbal).
    std::uint64_t AttitudeTimestampUs() const;
    double AircraftHeadingRad() const;

private:
    std::uint64_t NowUs() const;

    bool sub_quaternion_ = false;
    bool sub_velocity_ = false;
    bool sub_position_ = false;
    bool sub_gimbal_ = false;
    bool sub_flight_status_ = false;
    bool sub_rtk_ = false;
    bool sub_home_alt_ = false;
    bool sub_baro_ = false;

    mutable std::mutex mu_;
    core::AircraftState state_;
    GimbalAngles gimbal_;
    std::optional<int> flight_status_;
    std::optional<double> home_alt_m_;
    double baro_alt_m_ = 0.0;
    hal::GnssStatus gnss_;
    double last_heading_rad_ = 0.0;
    std::uint64_t attitude_ts_us_ = 0;

    // Staleness bookkeeping: the local time of the last poll in which the
    // DJI timestamp of POSITION_FUSED or QUATERNION advanced.
    std::uint64_t last_fresh_us_ = 0;
    // DJI timestamps packed ms<<20|us, used only to detect frozen data.
    std::uint64_t last_pos_dji_ts_ = 0;
    std::uint64_t last_quat_dji_ts_ = 0;

    bool have_origin_ = false;
    core::LatLonAlt origin_lla_{};
    core::geodesy::EnuOrigin enu_origin_;

    UpdateCallback update_cb_;
};

}  // namespace droppoint::psdk
