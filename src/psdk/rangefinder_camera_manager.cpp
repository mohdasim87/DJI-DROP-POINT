// psdk/rangefinder_camera_manager.cpp
//
// Laser rangefinder access via DjiCameraManager_GetLaserRangingInfo.
// Status: unverified — compiles against real headers on Linux, needs hardware.
#include "rangefinder_camera_manager.h"

#include "psdk_util.h"
#include "util/logging.h"

#ifdef __linux__
#include "dji_camera_manager.h"
#endif

namespace droppoint::psdk {

RangefinderCameraManager::RangefinderCameraManager(const util::config::AircraftConfig& cfg)
    : present_(cfg.rangefinder_present) {
    if (!present_) return;

#ifdef __linux__
    CheckReturn(DjiCameraManager_Init(), "DjiCameraManager_Init");
#else
    throw std::runtime_error("RangefinderCameraManager requires a Linux/PSDK build");
#endif
}

RangefinderCameraManager::~RangefinderCameraManager() {
#ifdef __linux__
    if (present_) {
        DjiCameraManager_DeInit();
    }
#endif
}

std::optional<hal::RangefinderReading> RangefinderCameraManager::GetLatestReading() {
    if (!present_) return std::nullopt;

#ifdef __linux__
    T_DjiCameraManagerLaserRangingInfo info{};
    const T_DjiReturnCode code = DjiCameraManager_GetLaserRangingInfo(mount_position_, &info);
    if (code != DJI_RETURN_CODE_OK) {
        DP_LOG_WARN("rangefinder_camera_manager",
                    "DjiCameraManager_GetLaserRangingInfo failed: 0x" + std::to_string(code));
        return std::nullopt;
    }

    if (!info.enable_lidar || info.exception != 0) {
        return std::nullopt;
    }

    hal::RangefinderReading reading{};
    reading.valid = true;
    reading.geodetic.lat_deg = static_cast<double>(info.latitude);
    reading.geodetic.lon_deg = static_cast<double>(info.longitude);
    reading.geodetic.alt_m_hae = static_cast<double>(info.altitude) * 0.1;
    reading.slant_range_m = static_cast<double>(info.distance) * 0.1;
    reading.timestamp_us = MonotonicUs();
    return reading;
#else
    return std::nullopt;
#endif
}

void RangefinderCameraManager::RequestMeasurement() {
    DP_LOG_DEBUG("rangefinder_camera_manager", "measurement requested (sample arrives via poll)");
}

}  // namespace droppoint::psdk
