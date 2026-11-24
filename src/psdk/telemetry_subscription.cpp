// psdk/telemetry_subscription.cpp — see header.
// Status: unverified — requires hardware.
#include "telemetry_subscription.h"

#include <dji_fc_subscription.h>
#include <dji_typedef.h>
#include <dji_widget.h>

#include <chrono>
#include <cmath>
#include <stdexcept>

#include "hw/conversions.h"
#include "util/logging.h"

// Pin the numeric values the DJI-free hw/conversions tables rely on, so a
// PSDK update that renumbers them fails here at compile time, not in the
// air.
static_assert(DJI_FC_SUBSCRIPTION_POSITION_SOLUTION_PROPERTY_NARROW_INT == 50,
              "RTK fix property changed; update hw/conversions.cpp");
static_assert(DJI_FC_SUBSCRIPTION_POSITION_SOLUTION_PROPERTY_FLOAT_L1_AMBIGUITY == 32 &&
                  DJI_FC_SUBSCRIPTION_POSITION_SOLUTION_PROPERTY_FLOAT_IONOSPHERIC_FREE_AMBIGUITY ==
                      33 &&
                  DJI_FC_SUBSCRIPTION_POSITION_SOLUTION_PROPERTY_FLOAT_SOLUTION == 34,
              "RTK float property values changed; update hw/conversions.cpp");
static_assert(DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_STOPED == 0 &&
                  DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_ON_GROUND == 1 &&
                  DJI_FC_SUBSCRIPTION_FLIGHT_STATUS_IN_AIR == 2,
              "flight status enum changed; update telemetry_subscription.cpp");
static_assert(DJI_AIRCRAFT_TYPE_M300_RTK == 60 && DJI_AIRCRAFT_TYPE_M350_RTK == 89 &&
                  DJI_AIRCRAFT_TYPE_M30 == 67 && DJI_AIRCRAFT_TYPE_M30T == 68 &&
                  DJI_AIRCRAFT_TYPE_M3E == 77 && DJI_AIRCRAFT_TYPE_M3T == 79 &&
                  DJI_AIRCRAFT_TYPE_M4T == 99 && DJI_AIRCRAFT_TYPE_M4E == 990 &&
                  DJI_AIRCRAFT_TYPE_M4TD == 100 && DJI_AIRCRAFT_TYPE_M4D == 1000 &&
                  DJI_AIRCRAFT_TYPE_M400 == 103,
              "aircraft type enum changed; update hw/conversions.cpp");
static_assert(DJI_WIDGET_TYPE_BUTTON == 1 && DJI_WIDGET_TYPE_SWITCH == 2 &&
                  DJI_WIDGET_TYPE_SCALE == 3 && DJI_WIDGET_TYPE_LIST == 4 &&
                  DJI_WIDGET_TYPE_INT_INPUT_BOX == 5,
              "widget type enum changed; update hw/widget_map.h");
static_assert(DJI_WIDGET_BUTTON_STATE_PRESS_DOWN == 1 && DJI_WIDGET_BUTTON_STATE_RELEASE_UP == 0,
              "widget button state enum changed; update hw/widget_map.h");

namespace droppoint::psdk {

namespace {

constexpr std::uint64_t PackDjiTs(const T_DjiDataTimestamp& ts) {
    // ~17.5 min of microsecond resolution per wrap; only equality matters.
    return (static_cast<std::uint64_t>(ts.millisecond) << 20) |
           (static_cast<std::uint64_t>(ts.microsecond) & 0xFFFFF);
}

bool QuaternionIsSane(const T_DjiFcSubscriptionQuaternion& q) {
    const double n = static_cast<double>(q.q0) * q.q0 + static_cast<double>(q.q1) * q.q1 +
                     static_cast<double>(q.q2) * q.q2 + static_cast<double>(q.q3) * q.q3;
    return std::isfinite(n) && n > 1e-6;
}

const char* TopicName(E_DjiFcSubscriptionTopic topic) {
    switch (topic) {
        case DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION: return "QUATERNION";
        case DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY: return "VELOCITY";
        case DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED: return "POSITION_FUSED";
        case DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES: return "GIMBAL_ANGLES";
        case DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT: return "STATUS_FLIGHT";
        case DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION_INFO: return "RTK_POSITION_INFO";
        case DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT: return "ALTITUDE_OF_HOMEPOINT";
        case DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_BAROMETER: return "ALTITUDE_BAROMETER";
        default: return "?";
    }
}

}  // namespace

void PsdkTelemetry::Init() {
    T_DjiReturnCode ret = DjiFcSubscription_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiFcSubscription_Init failed");
    }

    struct Plan {
        E_DjiFcSubscriptionTopic topic;
        bool required;
        bool* flag;
    };
    const Plan plan[] = {
        {DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION, true, &sub_quaternion_},
        {DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY, true, &sub_velocity_},
        {DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, true, &sub_position_},
        {DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES, true, &sub_gimbal_},
        {DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT, false, &sub_flight_status_},
        {DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION_INFO, false, &sub_rtk_},
        {DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT, false, &sub_home_alt_},
        {DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_BAROMETER, false, &sub_baro_},
    };

    for (const auto& p : plan) {
        ret = DjiFcSubscription_SubscribeTopic(p.topic, DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ, nullptr);
        if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS && p.required) {
            DP_LOG_WARN("telemetry", std::string("topic ") + TopicName(p.topic) +
                                        " rejected 50 Hz, retrying at 10 Hz");
            ret = DjiFcSubscription_SubscribeTopic(p.topic, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ,
                                                   nullptr);
        } else if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            // Optional topic: single attempt at 10 Hz keeps the freq-type
            // count low (PSDK allows at most 4 distinct frequencies).
            ret = DjiFcSubscription_SubscribeTopic(p.topic, DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ,
                                                   nullptr);
        }
        *p.flag = (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS);
        if (!*p.flag) {
            if (p.required) {
                throw std::runtime_error(std::string("required telemetry topic ") +
                                         TopicName(p.topic) +
                                         " could not be subscribed at 50 or 10 Hz");
            }
            DP_LOG_WARN("telemetry", std::string("optional topic ") + TopicName(p.topic) +
                                        " unavailable — continuing without it");
        }
    }
    last_fresh_us_ = NowUs();
}

std::vector<PsdkTelemetry::TopicStatusEntry> PsdkTelemetry::TopicStatus() const {
    struct Row {
        const char* name;
        bool ok;
        bool required;
        int hz;
    };
    const Row rows[] = {
        {"QUATERNION", sub_quaternion_, true, 50},
        {"VELOCITY", sub_velocity_, true, 50},
        {"POSITION_FUSED", sub_position_, true, 50},
        {"GIMBAL_ANGLES", sub_gimbal_, true, 50},
        {"STATUS_FLIGHT", sub_flight_status_, false, 10},
        {"RTK_POSITION_INFO", sub_rtk_, false, 10},
        {"ALTITUDE_OF_HOMEPOINT", sub_home_alt_, false, 10},
        {"ALTITUDE_BAROMETER", sub_baro_, false, 10},
    };
    std::vector<TopicStatusEntry> out;
    for (const auto& r : rows) {
        out.push_back({r.name, r.ok, r.ok ? r.hz : 0, r.required});
    }
    return out;
}

std::uint64_t PsdkTelemetry::NowUs() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void PsdkTelemetry::Poll() {
    const std::uint64_t now_us = NowUs();
    bool fresh = false;
    bool have_position = false;
    core::LatLonAlt lla{};
    int sats = 0;
    bool velocity_ok = false;
    core::EnuVector vel{};
    bool quat_ok = false;
    hw::conv::AttitudeRad att{};

    if (sub_quaternion_) {
        T_DjiFcSubscriptionQuaternion q{};
        T_DjiDataTimestamp ts{};
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                                                    reinterpret_cast<uint8_t*>(&q), sizeof(q),
                                                    &ts) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
            QuaternionIsSane(q)) {
            att = hw::conv::QuaternionFrdNedToEuler(q.q0, q.q1, q.q2, q.q3);
            quat_ok = std::isfinite(att.heading_rad);
            const std::uint64_t packed = PackDjiTs(ts);
            if (packed != last_quat_dji_ts_) {
                last_quat_dji_ts_ = packed;
                fresh = true;
            }
        }
    }

    if (sub_velocity_) {
        T_DjiFcSubscriptionVelocity v{};
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                                                    reinterpret_cast<uint8_t*>(&v), sizeof(v),
                                                    nullptr) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS &&
            v.health == DJI_FC_SUBSCRIPTION_DATA_HEALTH) {
            vel = hw::conv::NeuToEnu(v.data.x, v.data.y, v.data.z);
            velocity_ok = true;
        }
    }

    if (sub_position_) {
        T_DjiFcSubscriptionPositionFused pos{};
        T_DjiDataTimestamp ts{};
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                                    reinterpret_cast<uint8_t*>(&pos), sizeof(pos),
                                                    &ts) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            lla.lat_deg = hw::conv::RadToDeg(pos.latitude);
            lla.lon_deg = hw::conv::RadToDeg(pos.longitude);
            lla.alt_m_hae = pos.altitude;
            sats = pos.visibleSatelliteNumber;
            have_position = sats > 0;
            const std::uint64_t packed = PackDjiTs(ts);
            if (packed != last_pos_dji_ts_) {
                last_pos_dji_ts_ = packed;
                fresh = true;
            }
        }
    }

    if (sub_gimbal_) {
        T_DjiFcSubscriptionGimbalAngles g{};
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                                                    reinterpret_cast<uint8_t*>(&g), sizeof(g),
                                                    nullptr) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            std::lock_guard<std::mutex> lock(mu_);
            gimbal_.pitch_deg = g.x;
            gimbal_.roll_deg = g.y;
            gimbal_.yaw_deg = g.z;
            gimbal_.timestamp_us = now_us;
            gimbal_.valid = std::isfinite(g.x) && std::isfinite(g.y) && std::isfinite(g.z);
        }
    }

    if (sub_flight_status_) {
        T_DjiFcSubscriptionFlightStatus fs = 0;
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_STATUS_FLIGHT,
                                                    reinterpret_cast<uint8_t*>(&fs), sizeof(fs),
                                                    nullptr) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            flight_status_ = static_cast<int>(fs);
        }
    }

    if (sub_rtk_) {
        T_DjiFcSubscriptionRtkPositionInfo info = 0;
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION_INFO,
                                                    reinterpret_cast<uint8_t*>(&info), sizeof(info),
                                                    nullptr) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            gnss_.rtk_status = hw::conv::RtkFixStatusFromProperty(static_cast<int>(info));
            gnss_.rtk_connected = gnss_.rtk_status != hal::RtkFixStatus::kNotAvailable;
        }
    }

    if (sub_home_alt_) {
        T_DjiFcSubscriptionAltitudeOfHomePoint alt = 0.0f;
        if (DjiFcSubscription_GetLatestValueOfTopic(
                DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_OF_HOMEPOINT, reinterpret_cast<uint8_t*>(&alt),
                sizeof(alt), nullptr) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            home_alt_m_ = static_cast<double>(alt);
        }
    }

    if (sub_baro_) {
        T_DjiFcSubscriptionAltitudeBarometer alt = 0.0f;
        if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_BAROMETER,
                                                    reinterpret_cast<uint8_t*>(&alt), sizeof(alt),
                                                    nullptr) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            baro_alt_m_ = static_cast<double>(alt);
        }
    }

    // ENU origin latches on the first fix with >= 6 satellites.
    if (!have_origin_ && have_position && sats >= 6) {
        origin_lla_ = lla;
        enu_origin_ = core::geodesy::MakeEnuOrigin(lla);
        have_origin_ = true;
        DP_LOG_INFO("telemetry", "ENU origin latched at lat " + std::to_string(lla.lat_deg) +
                                     " lon " + std::to_string(lla.lon_deg));
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        core::AircraftState& s = state_;
        s.geodetic = lla;
        if (have_origin_ && have_position) {
            const auto enu = core::geodesy::LlaToEnu(lla, enu_origin_);
            s.position = {enu.e, enu.n, enu.u};
        }
        if (velocity_ok) s.velocity = vel;
        if (quat_ok) {
            s.yaw_rad = hw::conv::EnuYawFromHeadingRad(att.heading_rad);
            s.pitch_rad = att.pitch_rad;
            s.roll_rad = att.roll_rad;
            last_heading_rad_ = att.heading_rad;
            attitude_ts_us_ = now_us;
        }
        s.valid = quat_ok && velocity_ok && have_position && have_origin_;
        gnss_.satellite_count = sats;
    }

    if (fresh) {
        last_fresh_us_ = now_us;
        std::lock_guard<std::mutex> lock(mu_);
        state_.timestamp_us = now_us;
        if (update_cb_) update_cb_(state_);
    }
}

core::AircraftState PsdkTelemetry::GetAircraftState() {
    Poll();
    std::lock_guard<std::mutex> lock(mu_);
    return state_;
}

hal::GnssStatus PsdkTelemetry::GetGnssStatus() {
    std::lock_guard<std::mutex> lock(mu_);
    return gnss_;
}

double PsdkTelemetry::GetBarometricAltitudeM() { return baro_alt_m_; }

std::uint64_t PsdkTelemetry::GetTelemetryAgeUs() {
    const std::uint64_t now = NowUs();
    return now > last_fresh_us_ ? now - last_fresh_us_ : 0;
}

core::WindSample PsdkTelemetry::GetWind() {
    core::WindSample w;
    w.valid = false;  // no PSDK wind topic — the app-level estimator handles it
    return w;
}

std::vector<hal::RcButtonEvent> PsdkTelemetry::PollRcButtonEvents() { return {}; }

void PsdkTelemetry::RegisterUpdateCallback(UpdateCallback callback) {
    std::lock_guard<std::mutex> lock(mu_);
    update_cb_ = std::move(callback);
}

bool PsdkTelemetry::HasEnuOrigin() const { return have_origin_; }
core::LatLonAlt PsdkTelemetry::EnuOriginLla() const { return origin_lla_; }

PsdkTelemetry::GimbalAngles PsdkTelemetry::LatestGimbalAngles() const {
    std::lock_guard<std::mutex> lock(mu_);
    return gimbal_;
}

std::uint64_t PsdkTelemetry::AttitudeTimestampUs() const {
    std::lock_guard<std::mutex> lock(mu_);
    return attitude_ts_us_;
}
double PsdkTelemetry::AircraftHeadingRad() const {
    std::lock_guard<std::mutex> lock(mu_);
    return last_heading_rad_;
}

std::optional<int> PsdkTelemetry::LatestFlightStatus() const { return flight_status_; }
std::optional<double> PsdkTelemetry::LatestHomeAltitudeM() const { return home_alt_m_; }

}  // namespace droppoint::psdk
