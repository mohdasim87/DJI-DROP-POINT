// tests/test_operator_inputs.cpp
//
// The widget-event -> CycleInputs mapping: every widget, press edge rules,
// drain semantics (one-shot events cleared, levels kept) and out-of-range
// rejection.
#include "test_framework.h"

#include "hw/operator_inputs.h"
#include "hw/widget_map.h"

namespace {

using droppoint::app::ReleaseMode;
using droppoint::hw::OperatorInputs;
using droppoint::hw::WidgetType;

constexpr int kBtn = static_cast<int>(WidgetType::kButton);
constexpr int kSw = static_cast<int>(WidgetType::kSwitch);
constexpr int kList = static_cast<int>(WidgetType::kList);
constexpr int kBox = static_cast<int>(WidgetType::kIntInputBox);

}  // namespace

TEST_CASE("OperatorInputs: each button maps on press-down only") {
    OperatorInputs in(3);
    in.OnWidgetEvent(0, kBtn, 1);
    in.OnWidgetEvent(1, kBtn, 1);
    in.OnWidgetEvent(3, kBtn, 1);
    in.OnWidgetEvent(4, kBtn, 1);
    in.OnWidgetEvent(5, kBtn, 1);
    auto out = in.Drain(1000);
    REQUIRE(out.set_target_pressed);
    REQUIRE(out.confirm_target_pressed);
    REQUIRE(out.manual_release_pressed);
    REQUIRE(out.hardware_abort_pressed);
    REQUIRE(out.clear_target_pressed);
    REQUIRE(out.now_us == 1000);
}

TEST_CASE("OperatorInputs: button release-up is ignored") {
    OperatorInputs in(3);
    in.OnWidgetEvent(0, kBtn, 0);  // RELEASE_UP
    in.OnWidgetEvent(3, kBtn, 0);
    const auto out = in.Drain(0);
    REQUIRE(!out.set_target_pressed);
    REQUIRE(!out.manual_release_pressed);
}

TEST_CASE("OperatorInputs: ARM switch produces arm and disarm edges") {
    OperatorInputs in(3);
    in.OnWidgetEvent(2, kSw, 1);
    auto out = in.Drain(0);
    REQUIRE(out.arm_pressed);
    REQUIRE(!out.disarm_pressed);
    in.OnWidgetEvent(2, kSw, 0);
    out = in.Drain(0);
    REQUIRE(out.disarm_pressed);
    REQUIRE(!out.arm_pressed);
}

TEST_CASE("OperatorInputs: MODE switch requests auto and manual") {
    OperatorInputs in(3);
    in.OnWidgetEvent(6, kSw, 1);
    auto out = in.Drain(0);
    REQUIRE(out.requested_mode.has_value());
    REQUIRE(*out.requested_mode == ReleaseMode::kAuto);
    in.OnWidgetEvent(6, kSw, 0);
    out = in.Drain(0);
    REQUIRE(*out.requested_mode == ReleaseMode::kManual);
}

TEST_CASE("OperatorInputs: payload list validated against profile count") {
    OperatorInputs in(2);
    in.OnWidgetEvent(7, kList, 1);
    in.OnWidgetEvent(7, kList, 5);   // out of range: ignored
    in.OnWidgetEvent(7, kList, -1);  // out of range: ignored
    const auto out = in.Drain(0);
    REQUIRE(out.requested_payload_index.has_value());
    REQUIRE(*out.requested_payload_index == 1);
}

TEST_CASE("OperatorInputs: temperature accepted only in -40..60") {
    OperatorInputs in(2);
    in.OnWidgetEvent(8, kBox, 25);
    auto out = in.Drain(0);
    REQUIRE(out.ambient_temperature_c.has_value());
    REQUIRE(*out.ambient_temperature_c == 25.0);
    in.OnWidgetEvent(8, kBox, -41);
    in.OnWidgetEvent(8, kBox, 61);
    out = in.Drain(0);
    REQUIRE(!out.ambient_temperature_c.has_value());
    in.OnWidgetEvent(8, kBox, -40);
    in.OnWidgetEvent(8, kBox, 60);
    out = in.Drain(0);
    REQUIRE(out.ambient_temperature_c.has_value());
    REQUIRE(*out.ambient_temperature_c == 60.0);
}

TEST_CASE("OperatorInputs: surface list maps to wind alpha") {
    OperatorInputs in(2);
    in.OnWidgetEvent(9, kList, 0);
    auto out = in.Drain(0);
    REQUIRE(out.requested_wind_alpha.has_value());
    REQUIRE_NEAR(*out.requested_wind_alpha, 0.10, 1e-9);
    in.OnWidgetEvent(9, kList, 3);
    out = in.Drain(0);
    REQUIRE_NEAR(*out.requested_wind_alpha, 0.27, 1e-9);
    in.OnWidgetEvent(9, kList, 4);  // out of range
    out = in.Drain(0);
    REQUIRE(!out.requested_wind_alpha.has_value());
}

TEST_CASE("OperatorInputs: drain clears one-shot events but keeps levels") {
    OperatorInputs in(2);
    in.OnWidgetEvent(0, kBtn, 1);
    auto out = in.Drain(0);
    REQUIRE(out.set_target_pressed);
    out = in.Drain(0);
    REQUIRE(!out.set_target_pressed);  // one-shot cleared

    // Level state (ARM reported position) persists across drains.
    OperatorInputs::Status st;
    st.armed_or_countdown = true;
    st.mode = ReleaseMode::kAuto;
    st.active_payload_index = 1;
    st.ambient_temperature_c = 22.0;
    st.surface_index = 2;
    in.UpdateStatus(st);
    REQUIRE(in.GetWidgetValue(2) == 1);
    REQUIRE(in.GetWidgetValue(6) == 1);
    REQUIRE(in.GetWidgetValue(7) == 1);
    REQUIRE(in.GetWidgetValue(8) == 22);
    REQUIRE(in.GetWidgetValue(9) == 2);
    (void)in.Drain(0);
    REQUIRE(in.GetWidgetValue(2) == 1);  // still armed after the drain
}

TEST_CASE("OperatorInputs: unknown widget index is ignored") {
    OperatorInputs in(2);
    in.OnWidgetEvent(42, kBtn, 1);
    const auto out = in.Drain(0);
    REQUIRE(!out.set_target_pressed);
    REQUIRE(!out.arm_pressed);
}
