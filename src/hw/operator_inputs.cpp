// hw/operator_inputs.cpp
#include "operator_inputs.h"

#include "core/wind.h"

namespace droppoint::hw {

void OperatorInputs::OnWidgetEvent(int index, int type, std::int32_t value) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto widget_type = static_cast<WidgetType>(type);

    switch (index) {
        case kIdxSetTarget:
            if (widget_type == WidgetType::kButton && value == kButtonPressDown)
                pending_.set_target_pressed = true;
            break;
        case kIdxConfirm:
            if (widget_type == WidgetType::kButton && value == kButtonPressDown)
                pending_.confirm_target_pressed = true;
            break;
        case kIdxArm:
            if (widget_type == WidgetType::kSwitch) {
                if (value == 1) pending_.arm_pressed = true;
                if (value == 0) pending_.disarm_pressed = true;
            }
            break;
        case kIdxRelease:
            if (widget_type == WidgetType::kButton && value == kButtonPressDown)
                pending_.manual_release_pressed = true;
            break;
        case kIdxAbort:
            if (widget_type == WidgetType::kButton && value == kButtonPressDown)
                pending_.hardware_abort_pressed = true;
            break;
        case kIdxClearTarget:
            if (widget_type == WidgetType::kButton && value == kButtonPressDown)
                pending_.clear_target_pressed = true;
            break;
        case kIdxMode:
            if (widget_type == WidgetType::kSwitch) {
                pending_.requested_mode =
                    value == 1 ? app::ReleaseMode::kAuto : app::ReleaseMode::kManual;
            }
            break;
        case kIdxPayload:
            if (widget_type == WidgetType::kList && value >= 0 && value < payload_count_) {
                pending_.requested_payload_index = static_cast<int>(value);
            }
            break;
        case kIdxTempC:
            if (widget_type == WidgetType::kIntInputBox && value >= kTempMinC && value <= kTempMaxC) {
                pending_.ambient_temperature_c = static_cast<double>(value);
            }
            break;
        case kIdxSurface:
            if (widget_type == WidgetType::kList && value >= 0 && value < kSurfaceItemCount) {
                pending_.requested_wind_alpha =
                    core::wind::AlphaForSurface(static_cast<core::wind::SurfaceType>(value));
                pending_.requested_surface_index = static_cast<int>(value);
            }
            break;
        default:
            break;
    }
}

void OperatorInputs::UpdateStatus(const Status& status) {
    std::lock_guard<std::mutex> lock(mu_);
    status_ = status;
}

app::GuidanceLoop::CycleInputs OperatorInputs::Drain(std::uint64_t now_us) {
    std::lock_guard<std::mutex> lock(mu_);
    app::GuidanceLoop::CycleInputs out = pending_;
    pending_ = app::GuidanceLoop::CycleInputs{};
    out.now_us = now_us;
    return out;
}

std::int32_t OperatorInputs::GetWidgetValue(int index) const {
    std::lock_guard<std::mutex> lock(mu_);
    switch (index) {
        case kIdxArm:
            return status_.armed_or_countdown ? 1 : 0;
        case kIdxMode:
            return status_.mode == app::ReleaseMode::kAuto ? 1 : 0;
        case kIdxPayload:
            return status_.active_payload_index;
        case kIdxTempC:
            return static_cast<std::int32_t>(status_.ambient_temperature_c);
        case kIdxSurface:
            return status_.surface_index;
        default:
            return 0;  // buttons report released
    }
}

}  // namespace droppoint::hw
