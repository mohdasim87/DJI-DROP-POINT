// psdk/display_floating_window.h
//
// Floating-window text channel via DjiWidgetFloatingWindow_ShowMessage.
#pragma once

#include "hal/i_display.h"

namespace droppoint::psdk {

class DisplayFloatingWindow : public hal::IDisplay {
public:
    DisplayFloatingWindow();
    ~DisplayFloatingWindow() override;
    void ShowMessage(const std::string& text) override;
};

}  // namespace droppoint::psdk
