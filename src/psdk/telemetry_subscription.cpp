// psdk/telemetry_subscription.cpp
//
// Real aircraft telemetry via DJI PSDK Data Subscription topics.
// Status: unverified — compiles against real headers, needs hardware to verify.
#include "telemetry_subscription.h"

#include <cmath>
#include <cstring>

#include "core/atmosphere.h"
#include "core/math_constants.h"
#include "core/wind.h"
#include "psdk_util.h"
#include "util/logging.h"

#include "dji_fc_subscription.h"

namespace droppoint::psdk {

namespace {

// The PSDK subscription callback signature has no user-data argument, so a
// single static pointer points to the active instance. This is safe because
// DropPoint creates exactly one TelemetrySubscription per process.
TelemetrySubscription* g_instance = nullptr;

std::uint64_t DjiTimestampToUs(const T_DjiDataTimestamp* ts) {
    if (!ts) return 0;
    return static_cast<std::uint64_t>(ts->millisecond) * 1000ULL +
           static_cast<std::uint64_t>(ts->microsecond);
}

extern "C" T_DjiReturnCode QuaternionCallback(const uint8_t* data, uint16_t dataSize,
                                               const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionQuaternion)) return DJI_RETURN_CODE_OK;
    const auto* q = reinterpret_cast<const T_DjiFcSubscriptionQuaternion*>(data);
    g_instance->OnQuaternion(*q, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode VelocityCallback(const uint8_t* data, uint16_t dataSize,
                                            const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionVelocity)) return DJI_RETURN_CODE_OK;
    const auto* v = reinterpret_cast<const T_DjiFcSubscriptionVelocity*>(data);
    g_instance->OnVelocity(*v, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode PositionFusedCallback(const uint8_t* data, uint16_t dataSize,
                                                 const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionPositionFused)) return DJI_RETURN_CODE_OK;
    const auto* p = reinterpret_cast<const T_DjiFcSubscriptionPositionFused*>(data);
    g_instance->OnPositionFused(*p, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode AltitudeFusedCallback(const uint8_t* data, uint16_t dataSize,
                                                 const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionAltitudeFused)) return DJI_RETURN_CODE_OK;
    const auto* a = reinterpret_cast<const T_DjiFcSubscriptionAltitudeFused*>(data);
    g_instance->OnAltitudeFused(*a, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode AccelerationGroundCallback(const uint8_t* data, uint16_t dataSize,
                                                       const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionAccelerationGround)) return DJI_RETURN_CODE_OK;
    const auto* a = reinterpret_cast<const T_DjiFcSubscriptionAccelerationGround*>(data);
    g_instance->OnAccelerationGround(*a, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode GpsDetailsCallback(const uint8_t* data, uint16_t dataSize,
                                              const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionGpsDetails)) return DJI_RETURN_CODE_OK;
    const auto* d = reinterpret_cast<const T_DjiFcSubscriptionGpsDetails*>(data);
    g_instance->OnGpsDetails(*d, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode RtkPositionCallback(const uint8_t* data, uint16_t dataSize,
                                               const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionRtkPosition)) return DJI_RETURN_CODE_OK;
    const auto* p = reinterpret_cast<const T_DjiFcSubscriptionRtkPosition*>(data);
    g_instance->OnRtkPosition(*p, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode RtkYawCallback(const uint8_t* data, uint16_t dataSize,
                                          const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionRtkYaw)) return DJI_RETURN_CODE_OK;
    const auto* y = reinterpret_cast<const T_DjiFcSubscriptionRtkYaw*>(data);
    g_instance->OnRtkYaw(*y, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode GimbalAnglesCallback(const uint8_t* data, uint16_t dataSize,
                                                const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiVector3f)) return DJI_RETURN_CODE_OK;
    const auto* angles = reinterpret_cast<const T_DjiVector3f*>(data);
    g_instance->OnGimbalAngles(*angles, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode RcCallback(const uint8_t* data, uint16_t dataSize,
                                    const T_DjiDataTimestamp* timestamp) {
    if (!g_instance || dataSize < sizeof(T_DjiFcSubscriptionRC)) return DJI_RETURN_CODE_OK;
    const auto* rc = reinterpret_cast<const T_DjiFcSubscriptionRC*>(data);
    g_instance->OnRc(*rc, DjiTimestampToUs(timestamp));
    return DJI_RETURN_CODE_OK;
}

}  // namespace

TelemetrySubscription::TelemetrySubscription(const util::config::AircraftConfig& aircraft_cfg,
                                             const util::config::SystemConfig& system_cfg,
                                             SharedEnuOrigin origin)
    : aircraft_cfg_(aircraft_cfg), system_cfg_(system_cfg), origin_(std::move(origin)) {
    if (g_instance) {
        throw std::runtime_error("only one TelemetrySubscription may exist per process");
    }
    g_instance = this;

    CheckReturn(DjiFcSubscription_Init(), "DjiFcSubscription_Init");

    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_QUATERNION,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
                                                 QuaternionCallback),
                "subscribe QUATERNION");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_VELOCITY,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
                                                 VelocityCallback),
                "subscribe VELOCITY");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
                                                 PositionFusedCallback),
                "subscribe POSITION_FUSED");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_ALTITUDE_FUSED,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
                                                 AltitudeFusedCallback),
                "subscribe ALTITUDE_FUSED");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_ACCELERATION_GROUND,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
                                                 AccelerationGroundCallback),
                "subscribe ACCELERATION_GROUND");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_DETAILS,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ,
                                                 GpsDetailsCallback),
                "subscribe GPS_DETAILS");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_RTK_POSITION,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ,
                                                 RtkPositionCallback),
                "subscribe RTK_POSITION");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_RTK_YAW,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_5_HZ,
                                                 RtkYawCallback),
                "subscribe RTK_YAW");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GIMBAL_ANGLES,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
                                                 GimbalAnglesCallback),
                "subscribe GIMBAL_ANGLES");
    CheckReturn(DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_RC,
                                                 DJI_DATA_SUBSCRIPTION_TOPIC_10_HZ,
                                                 RcCallback),
                "subscribe RC");

    DP_LOG_INFO("telemetry_subscription", "subscribed to PSDK FC topics");
}

TelemetrySubscription::~TelemetrySubscription() {
    DjiFcSubscription_DeInit();
    g_instance = nullptr;
}

void TelemetrySubscription::UpdateOriginLocked(const core::LatLonAlt& lla) {
    if (!origin_ || origin_->lla.lat_deg != 0.0) return;  // already set
    *origin_ = core::geodesy::MakeEnuOrigin(lla);
    DP_LOG_INFO("telemetry_subscription",
                std::string("set ENU origin to ") + std::to_string(lla.lat_deg) + ", " +
                    std::to_string(lla.lon_deg));
}

core::AircraftState TelemetrySubscription::BuildAircraftStateLocked() const {
    core::AircraftState state{};
    state.timestamp_us = last_update_us_;
    state.geodetic = geodetic_;

    if (origin_ && origin_->lla.lat_deg != 0.0) {
        state.position = core::geodesy::LlaToEnu(geodetic_, *origin_);
    }

    state.velocity = velocity_;

    if (have_quat_ && origin_ && origin_->lla.lat_deg != 0.0) {
        const core::AircraftState attitude_state = QuaternionToEnu(quat_, *origin_);
        state.yaw_rad = attitude_state.yaw_rad;
        state.pitch_rad = attitude_state.pitch_rad;
        state.roll_rad = attitude_state.roll_rad;
    }

    state.valid = origin_ && origin_->lla.lat_deg != 0.0 && have_quat_ && last_update_us_ > 0;
    return state;
}

core::AircraftState TelemetrySubscription::GetAircraftState() {
    std::lock_guard<std::mutex> lock(mutex_);
    return BuildAircraftStateLocked();
}

hal::GnssStatus TelemetrySubscription::GetGnssStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    hal::GnssStatus status{};
    if (have_gps_details_) {
        status.satellite_count = static_cast<int>(gps_details_.totalSatelliteNumberUsed);
    }
    status.rtk_connected = have_rtk_position_;
    status.rtk_status = rtk_fix_ ? hal::RtkFixStatus::kFix : hal::RtkFixStatus::kNotAvailable;
    return status;
}

double TelemetrySubscription::GetBarometricAltitudeM() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (have_altitude_) return static_cast<double>(altitude_fused_m_);
    return 0.0;
}

std::uint64_t TelemetrySubscription::GetTelemetryAgeUs() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t now = MonotonicUs();
    return (last_update_us_ <= now) ? (now - last_update_us_) : 0;
}

core::WindSample TelemetrySubscription::GetWind() {
    std::lock_guard<std::mutex> lock(mutex_);
    const core::AircraftState state = BuildAircraftStateLocked();
    if (!state.valid) return core::WindSample{};

    // Estimate wind from hover attitude. Only meaningful in a low-speed hover.
    core::wind::HoverEstimateInputs hover_in{};
    hover_in.pitch_rad = state.pitch_rad;
    hover_in.roll_rad = state.roll_rad;
    hover_in.yaw_rad = state.yaw_rad;
    hover_in.mass_kg = aircraft_cfg_.mass_kg;
    hover_in.cda_airframe = aircraft_cfg_.cda_airframe;
    hover_in.air_density_kg_m3 =
        core::atmosphere::AtAltitude(geodetic_.alt_m_hae, 25.0).density_kg_m3;

    return core::wind::EstimateFromHover(hover_in);
}

std::vector<hal::RcButtonEvent> TelemetrySubscription::PollRcButtonEvents() {
    // The PSDK does not expose a native RC button API. This adapter reads RC
    // channels, but a safe mapping from channel values/switches to the logical
    // buttons in config/aircraft.yaml requires per-operator configuration in
    // DJI Pilot and is left as a documented gap (docs/OPEN-QUESTIONS.md #3).
    return {};
}

void TelemetrySubscription::RegisterUpdateCallback(UpdateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back(std::move(callback));
}

void TelemetrySubscription::NotifyCallbacksLocked(const core::AircraftState& state) {
    for (const auto& cb : callbacks_) {
        cb(state);
    }
}

void TelemetrySubscription::OnQuaternion(const T_DjiFcSubscriptionQuaternion& q,
                                         std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    quat_ = q;
    have_quat_ = true;
    last_update_us_ = now_us;

    core::AircraftState state = BuildAircraftStateLocked();
    NotifyCallbacksLocked(state);
}

void TelemetrySubscription::OnVelocity(const T_DjiFcSubscriptionVelocity& v,
                                     std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    velocity_ = NeuToEnu(v.data);
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnPositionFused(const T_DjiFcSubscriptionPositionFused& p,
                                          std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    core::LatLonAlt lla;
    lla.lat_deg = static_cast<double>(p.latitude);
    lla.lon_deg = static_cast<double>(p.longitude);
    lla.alt_m_hae = static_cast<double>(p.altitude);
    geodetic_ = lla;
    UpdateOriginLocked(lla);
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnAltitudeFused(const float altitude_m, std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    altitude_fused_m_ = altitude_m;
    have_altitude_ = true;
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnAccelerationGround(const T_DjiVector3f& a,
                                                  std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    acceleration_ground_ = a;
    have_acceleration_ = true;
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnGpsDetails(const T_DjiFcSubscriptionGpsDetails& d,
                                         std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    gps_details_ = d;
    have_gps_details_ = true;
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnRtkPosition(const T_DjiFcSubscriptionRtkPosition& p,
                                          std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    rtk_position_ = p;
    have_rtk_position_ = true;
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnRtkYaw(const std::int16_t yaw_cdeg, std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)yaw_cdeg;
    have_rtk_yaw_ = true;
    last_update_us_ = now_us;
}

void TelemetrySubscription::OnGimbalAngles(const T_DjiVector3f& angles,
                                           std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)angles;
    (void)now_us;
    // Gimbal angles are exposed through IGimbal (gimbal_psdk.cpp), not here,
    // because the guidance loop consumes gimbal state separately from aircraft
    // state. Storing them would couple the two adapters.
}

void TelemetrySubscription::OnRc(const T_DjiFcSubscriptionRC& rc, std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_rc_ = rc;
    have_rc_ = true;
    last_update_us_ = now_us;
    (void)now_us;
}

}  // namespace droppoint::psdk
