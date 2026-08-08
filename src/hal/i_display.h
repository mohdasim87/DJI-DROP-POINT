// hal/i_display.h
//
// The floating-window text channel (spec section 9). Real implementation:
// DjiWidgetFloatingWindow_ShowMessage, 255-byte limit, 2 KB/s bandwidth
// (docs/PSDK-RESEARCH.md section 4). Updated at 2 Hz by app/guidance_loop.
#pragma once

#include <string>

namespace droppoint::hal {

inline constexpr std::size_t kFloatingWindowMaxBytes = 255;

class IDisplay {
public:
    virtual ~IDisplay() = default;

    // Truncates to kFloatingWindowMaxBytes if needed; implementations log a
    // warning if truncation actually occurs so message text can be trimmed
    // in app/ rather than silently losing information in the field.
    virtual void ShowMessage(const std::string& text) = 0;
};

}  // namespace droppoint::hal
