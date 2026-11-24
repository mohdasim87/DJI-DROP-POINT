// hw/widget_map.h
//
// The single source of truth for DropPoint's own Pilot 2 widget layout:
// which index is which control, its widget type, display name, interface
// section and RC-button mappability. Used by the widget_config.json
// generator (hw/widget_ui.*), the input latch (hw/operator_inputs.*) and
// the PSDK handler-list registration (src/psdk/widget_bridge.*).
//
// The type values deliberately match E_DjiWidgetType so the PSDK adapter
// can static_assert them instead of translating.
#pragma once

namespace droppoint::hw {

enum class WidgetType {
    kButton = 1,        // DJI_WIDGET_TYPE_BUTTON
    kSwitch = 2,        // DJI_WIDGET_TYPE_SWITCH
    kScale = 3,         // DJI_WIDGET_TYPE_SCALE
    kList = 4,          // DJI_WIDGET_TYPE_LIST
    kIntInputBox = 5,   // DJI_WIDGET_TYPE_INT_INPUT_BOX
};

struct WidgetDef {
    int index;
    WidgetType type;
    const char* name;      // <= 32 chars, shown in Pilot 2
    bool main_interface;   // main_interface widgets require an icon file
    const char* icon;      // file name under the widget dir, "" when none
    bool rc_mappable;      // customize_rc_buttons_config.is_enable
};

// Button events only fire on PRESS_DOWN (value == 1); switches and inputs
// report their level. Mapping of each index is documented in
// operator_inputs.h and docs/ (operator UI).
inline constexpr WidgetDef kWidgetTable[] = {
    {0, WidgetType::kButton,       "SET TARGET",   true, "icon_set_target.png",   true},
    {1, WidgetType::kButton,       "CONFIRM",      true, "icon_confirm.png",      true},
    {2, WidgetType::kSwitch,       "ARM",          true, "icon_arm.png",          true},
    {3, WidgetType::kButton,       "RELEASE",      true, "icon_release.png",      true},
    {4, WidgetType::kButton,       "ABORT",        true, "icon_abort.png",        true},
    {5, WidgetType::kButton,       "CLEAR TARGET", false, "",                     true},
    {6, WidgetType::kSwitch,       "MODE AUTO",    false, "",                     true},
    {7, WidgetType::kList,         "PAYLOAD",      false, "",                     false},
    {8, WidgetType::kIntInputBox,  "TEMP C",       false, "",                     false},
    {9, WidgetType::kList,         "SURFACE",      false, "",                     false},
};
inline constexpr int kWidgetCount = static_cast<int>(sizeof(kWidgetTable) / sizeof(kWidgetTable[0]));

// Index constants used by the input mapping (keep in sync with the table).
inline constexpr int kIdxSetTarget = 0;
inline constexpr int kIdxConfirm = 1;
inline constexpr int kIdxArm = 2;
inline constexpr int kIdxRelease = 3;
inline constexpr int kIdxAbort = 4;
inline constexpr int kIdxClearTarget = 5;
inline constexpr int kIdxMode = 6;
inline constexpr int kIdxPayload = 7;
inline constexpr int kIdxTempC = 8;
inline constexpr int kIdxSurface = 9;

// SURFACE list order == core::wind::SurfaceType declaration order.
inline constexpr const char* kSurfaceItems[] = {
    "OPEN WATER / DESERT",
    "OPEN COUNTRY",
    "BROKEN COUNTRY",
    "URBAN / FOREST",
};
inline constexpr int kSurfaceItemCount = 4;

// Button PRESS_DOWN/RELEASE_UP values (mirror E_DjiWidgetButtonState).
inline constexpr int kButtonPressDown = 1;
inline constexpr int kButtonReleaseUp = 0;

// Accepted range for the TEMP C input box.
inline constexpr int kTempMinC = -40;
inline constexpr int kTempMaxC = 60;

}  // namespace droppoint::hw
