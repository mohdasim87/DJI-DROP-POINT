// psdk/display_floating_window.cpp
//
// Floating-window text channel via DjiWidgetFloatingWindow_ShowMessage.
// Status: unverified — compiles against real headers, needs hardware to verify.
#include "display_floating_window.h"

#include <algorithm>
#include <cstring>

#include "psdk_util.h"
#include "util/logging.h"

#include "dji_widget.h"

namespace droppoint::psdk {

DisplayFloatingWindow::DisplayFloatingWindow() {
    CheckReturn(DjiWidget_Init(), "DjiWidget_Init");
}

DisplayFloatingWindow::~DisplayFloatingWindow() = default;

void DisplayFloatingWindow::ShowMessage(const std::string& text) {
    // The PSDK floating window is limited to 255 bytes. Truncate cleanly and
    // warn so operators can see if important fields are being lost.
    std::string out = text;
    if (out.size() > hal::kFloatingWindowMaxBytes) {
        DP_LOG_WARN("display_floating_window",
                    "message truncated from " + std::to_string(text.size()) + " to " +
                        std::to_string(hal::kFloatingWindowMaxBytes) + " bytes");
        out.resize(hal::kFloatingWindowMaxBytes);
    }

    const T_DjiReturnCode code = DjiWidgetFloatingWindow_ShowMessage(out.c_str());
    if (code != DJI_RETURN_CODE_OK) {
        DP_LOG_WARN("display_floating_window",
                    "DjiWidgetFloatingWindow_ShowMessage failed: 0x" + std::to_string(code));
    }
}

}  // namespace droppoint::psdk
