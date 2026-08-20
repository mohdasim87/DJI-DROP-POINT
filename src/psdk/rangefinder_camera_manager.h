// psdk/rangefinder_camera_manager.h
//
// Laser rangefinder access via DjiCameraManager_GetLaserRangingInfo.
// Status: unverified.
#pragma once

#include <mutex>
#include <optional>

#include "hal/i_rangefinder.h"
#include "util/config.h"

#include "dji_typedef.h"

namespace droppoint::psdk {

class RangefinderCameraManager : public hal::IRangefinder {
public:
    explicit RangefinderCameraManager(const util::config::AircraftConfig& cfg);
    ~RangefinderCameraManager() override;

    // IRangefinder implementation.
    std::optional<hal::RangefinderReading> GetLatestReading() override;
    void RequestMeasurement() override;

private:
    bool present_ = false;
    E_DjiMountPosition mount_position_ = DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1;
};

}  // namespace droppoint::psdk
