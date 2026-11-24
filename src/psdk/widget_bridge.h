// psdk/widget_bridge.h
//
// Status: unverified — requires hardware.
//
// Registers DropPoint's own Pilot 2 widgets (layout in hw/widget_map.h) and
// bridges the widget callbacks into hw::OperatorInputs, which the guidance
// loop drains once per tick.
#pragma once

#include <string>

namespace droppoint::hw {
class OperatorInputs;
}

namespace droppoint::psdk {

// DjiWidget_Init + DjiWidget_RegDefaultUiConfigByDirPath(widget_dir) +
// DjiWidget_RegHandlerList for all 10 widgets. The callbacks funnel into
// `inputs`; `inputs` must outlive the registration (i.e. program lifetime).
// Throws std::runtime_error on failure.
void RegisterOperatorWidgets(hw::OperatorInputs& inputs, const std::string& widget_dir);

}  // namespace droppoint::psdk
