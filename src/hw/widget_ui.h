// hw/widget_ui.h
//
// Generates the widget directory DropPoint registers with
// DjiWidget_RegDefaultUiConfigByDirPath at startup: a widget_config.json
// matching the DJI widget-file format (see psdk-upstream
// samples/.../widget_file/en_big_screen/widget_config.json) plus the icon
// PNGs required by main_interface widgets.
#pragma once

#include <string>
#include <vector>

namespace droppoint::hw {

// Writes `out_dir`/widget_config.json and copies every icon referenced by
// main_interface widgets from `icon_dir` into `out_dir`. `payload_names`
// becomes the PAYLOAD list's items, in order (index 7).
// Throws std::runtime_error on any I/O failure.
void GenerateWidgetDir(const std::string& out_dir, const std::string& icon_dir,
                       const std::vector<std::string>& payload_names);

// The JSON text alone (exposed so tests can validate it without touching
// the filesystem, and so a caller can embed it elsewhere).
std::string BuildWidgetConfigJson(const std::vector<std::string>& payload_names);

}  // namespace droppoint::hw
