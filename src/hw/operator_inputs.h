// hw/operator_inputs.h
//
// Thread-safe latch between the Pilot 2 widget callbacks (fired on a PSDK
// worker thread) and the 10 Hz guidance loop. Widget events are latched as
// they arrive; Drain() converts them into one CycleInputs per tick.
//
// Widget mapping (hw/widget_map.h):
//   0 SET TARGET   button  -> set_target_pressed        (PRESS_DOWN only)
//   1 CONFIRM      button  -> confirm_target_pressed    (PRESS_DOWN only)
//   2 ARM          switch  -> on: arm_pressed, off: disarm_pressed
//   3 RELEASE      button  -> manual_release_pressed    (PRESS_DOWN only)
//   4 ABORT        button  -> hardware_abort_pressed    (PRESS_DOWN only)
//   5 CLEAR TARGET button  -> clear_target_pressed      (PRESS_DOWN only)
//   6 MODE         switch  -> requested_mode (off = manual, on = auto)
//   7 PAYLOAD      list    -> requested_payload_index   (validated vs count)
//   8 TEMP C       int box -> ambient_temperature_c     (-40..60 only)
//   9 SURFACE      list    -> requested_wind_alpha (core::wind::AlphaForSurface)
//
// Buttons/edge events are one-shot: Drain() returns them once and clears
// them. Level values (mode switch, payload index, temperature, surface)
// are reported once per change and are re-readable via GetWidgetValue.
#pragma once

#include <cstdint>
#include <mutex>

#include "app/guidance_loop.h"
#include "widget_map.h"

namespace droppoint::hw {

class OperatorInputs {
public:
    // Live state reported back to Pilot 2 through the GetWidgetValue
    // callbacks — updated by main each tick from the mission state.
    struct Status {
        bool armed_or_countdown = false;
        app::ReleaseMode mode = app::ReleaseMode::kManual;
        int active_payload_index = 0;
        double ambient_temperature_c = 15.0;
        int surface_index = 1;  // kOpenCountry
    };

    explicit OperatorInputs(int payload_count) : payload_count_(payload_count) {}

    // Called from the widget SetWidgetValue callback (any thread).
    // `type` is the E_DjiWidgetType-compatible WidgetType value.
    void OnWidgetEvent(int index, int type, std::int32_t value);

    // Called by main once per tick before Drain, so Pilot 2 reads back live
    // mission state rather than stale switch positions.
    void UpdateStatus(const Status& status);

    // Drains all latched events into one CycleInputs. One-shot events are
    // cleared; levels persist in the Status used by GetWidgetValue.
    app::GuidanceLoop::CycleInputs Drain(std::uint64_t now_us);

    // Value reported to Pilot 2 for read-back widgets (switches, list,
    // int input box). Buttons report released (0).
    std::int32_t GetWidgetValue(int index) const;

private:
    const int payload_count_;
    mutable std::mutex mu_;

    app::GuidanceLoop::CycleInputs pending_{};
    Status status_;
};

}  // namespace droppoint::hw
