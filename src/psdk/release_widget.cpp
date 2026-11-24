// psdk/release_widget.cpp — see header. Status: unverified — requires hardware.
#include "release_widget.h"

#include <dji_typedef.h>
#include <dji_widget.h>
#include <dji_widget_manager.h>

#include <chrono>
#include <stdexcept>

#include "util/logging.h"

namespace droppoint::psdk {

PsdkWidgetRelease::PsdkWidgetRelease(const util::config::ReleaseConfig& config)
    : config_(config), is_switch_(config.widget_type == "switch") {}

PsdkWidgetRelease::~PsdkWidgetRelease() {
    SetSafe();
    if (reset_thread_.joinable()) reset_thread_.join();
}

void PsdkWidgetRelease::Init() {
    const T_DjiReturnCode ret = DjiWidgetManager_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiWidgetManager_Init failed — Widget Manager is M400/Linux "
                                 "only");
    }
    // Buttons start at safe_value conceptually; only a switch needs its
    // level written on init.
    if (is_switch_) SetWidget(config_.widget_safe_value);
}

bool PsdkWidgetRelease::SetWidget(std::int32_t value) {
    T_DjiWidgetStates states{};
    states.widgetType =
        is_switch_ ? DJI_WIDGET_TYPE_SWITCH : DJI_WIDGET_TYPE_BUTTON;
    states.widgetIndex = static_cast<uint8_t>(config_.widget_index);
    states.widgetValue = value;
    const T_DjiReturnCode ret = DjiWidgetManager_SetWidgetState(
        static_cast<E_DjiMountPosition>(config_.widget_mount_position), states);
    responding_ = (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS);
    return responding_;
}

void PsdkWidgetRelease::Fire() {
    if (state_.exchange(hal::ReleaseState::kFiring) != hal::ReleaseState::kSafe) {
        return;  // single-shot contract: only fire from kSafe
    }
    if (!SetWidget(config_.widget_fire_value)) {
        state_ = hal::ReleaseState::kSafe;
        DP_LOG_ERROR("release", "psdk_widget Fire: SetWidgetState failed");
        return;
    }
    if (is_switch_) {
        state_ = hal::ReleaseState::kFired;
        return;
    }
    // Momentary button: release it 200 ms later so the kit sees a real
    // press rather than a stuck button.
    if (reset_thread_.joinable()) reset_thread_.join();
    reset_pending_ = true;
    reset_thread_ = std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (reset_pending_.exchange(false)) {
            SetWidget(0);
        }
    });
    state_ = hal::ReleaseState::kFired;
}

void PsdkWidgetRelease::SetSafe() {
    reset_pending_ = false;
    if (is_switch_) {
        SetWidget(config_.widget_safe_value);
    }
    state_ = hal::ReleaseState::kSafe;
}

}  // namespace droppoint::psdk
