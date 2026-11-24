// psdk/widget_bridge.cpp — see header. Status: unverified — requires hardware.
#include "widget_bridge.h"

#include <dji_widget.h>
#include <dji_typedef.h>

#include <array>
#include <stdexcept>

#include "hw/operator_inputs.h"
#include "hw/widget_map.h"
#include "util/logging.h"

namespace droppoint::psdk {

namespace {

// The OperatorInputs pointer travels via T_DjiWidgetHandlerListItem.userData
// (verified in dji_widget.h) — no global instance pointer needed.
T_DjiReturnCode SetWidgetValue(E_DjiWidgetType widgetType, uint32_t index, int32_t value,
                               void* userData) {
    auto* inputs = static_cast<hw::OperatorInputs*>(userData);
    if (inputs == nullptr) return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    inputs->OnWidgetEvent(static_cast<int>(index), static_cast<int>(widgetType), value);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode GetWidgetValue(E_DjiWidgetType widgetType, uint32_t index, int32_t* value,
                               void* userData) {
    (void)widgetType;
    auto* inputs = static_cast<hw::OperatorInputs*>(userData);
    if (inputs == nullptr || value == nullptr) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }
    *value = inputs->GetWidgetValue(static_cast<int>(index));
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

}  // namespace

void RegisterOperatorWidgets(hw::OperatorInputs& inputs, const std::string& widget_dir) {
    T_DjiReturnCode ret = DjiWidget_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiWidget_Init failed");
    }

    ret = DjiWidget_RegDefaultUiConfigByDirPath(widget_dir.c_str());
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiWidget_RegDefaultUiConfigByDirPath failed for " + widget_dir);
    }

    // One handler item per widget in hw::kWidgetTable — registration order
    // mirrors the DJI test_widget sample: Init -> RegDefaultUiConfigByDirPath
    // -> RegHandlerList.
    std::array<T_DjiWidgetHandlerListItem, hw::kWidgetCount> handlers{};
    for (int i = 0; i < hw::kWidgetCount; ++i) {
        handlers[i].widgetIndex = static_cast<uint32_t>(hw::kWidgetTable[i].index);
        handlers[i].widgetType = static_cast<E_DjiWidgetType>(hw::kWidgetTable[i].type);
        handlers[i].SetWidgetValue = SetWidgetValue;
        handlers[i].GetWidgetValue = GetWidgetValue;
        handlers[i].userData = &inputs;
    }

    ret = DjiWidget_RegHandlerList(handlers.data(), handlers.size());
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiWidget_RegHandlerList failed");
    }
    DP_LOG_INFO("widgets", "registered " + std::to_string(hw::kWidgetCount) +
                               " operator widgets from " + widget_dir);
}

}  // namespace droppoint::psdk
