// psdk/rangefinder_camera_manager.cpp — see header.
// Status: unverified — requires hardware.
#include "rangefinder_camera_manager.h"

#include <dji_camera_manager.h>
#include <dji_typedef.h>

#include <chrono>
#include <stdexcept>

#include "util/logging.h"

namespace droppoint::psdk {

namespace {

// The laser rangefinder is a spot sensor — 2 Hz is already generous for the
// hover-designate flow and keeps the camera-manager channel quiet.
constexpr std::uint64_t kMinPollIntervalUs = 500000;

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

PsdkRangefinder::PsdkRangefinder(int mount_position) : mount_position_(mount_position) {}

void PsdkRangefinder::Init() {
    const T_DjiReturnCode ret = DjiCameraManager_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiCameraManager_Init failed — camera management needs the "
                                 "advanced feature set (Manifold 3)");
    }
    initialised_ = true;
}

std::optional<hal::RangefinderReading> PsdkRangefinder::GetLatestReading() {
    const std::uint64_t now = NowUs();
    if (initialised_ && measurement_requested_ && now - last_poll_us_ >= kMinPollIntervalUs) {
        last_poll_us_ = now;
        measurement_requested_ = false;

        T_DjiCameraManagerLaserRangingInfo info{};
        const T_DjiReturnCode ret = DjiCameraManager_GetLaserRangingInfo(
            static_cast<E_DjiMountPosition>(mount_position_), &info);
        // lat/lon are degrees; altitude/distance are int32 in 0.1 m units;
        // a reading only counts when the call succeeds, distance > 0 and no
        // sensor exception is flagged.
        if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS && info.distance > 0 &&
            info.exception == 0) {
            latest_.valid = true;
            latest_.geodetic.lat_deg = info.latitude;
            latest_.geodetic.lon_deg = info.longitude;
            latest_.geodetic.alt_m_hae = static_cast<double>(info.altitude) * 0.1;
            latest_.slant_range_m = static_cast<double>(info.distance) * 0.1;
            latest_.timestamp_us = now;
            have_latest_ = true;
        } else {
            latest_.valid = false;
        }
    }
    if (!have_latest_) return std::nullopt;
    return latest_;
}

void PsdkRangefinder::RequestMeasurement() { measurement_requested_ = true; }

}  // namespace droppoint::psdk
