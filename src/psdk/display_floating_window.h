// psdk/display_floating_window.h
//
// Status: unverified — requires hardware.
//
// hal::IDisplay backed by DjiWidgetFloatingWindow_ShowMessage — the Pilot 2
// floating window. Available on both airframes (widget support is a basic
// feature). Messages are truncated to hal::kFloatingWindowMaxBytes with a
// logged warning, per the HAL contract.
#pragma once

#include "hal/i_display.h"

namespace droppoint::psdk {

class FloatingWindowDisplay : public hal::IDisplay {
public:
    void ShowMessage(const std::string& text) override;
};

}  // namespace droppoint::psdk
