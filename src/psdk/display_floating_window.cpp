// psdk/display_floating_window.cpp — see header.
// Status: unverified — requires hardware.
#include "display_floating_window.h"

#include <dji_widget.h>

#include "util/logging.h"

namespace droppoint::psdk {

void FloatingWindowDisplay::ShowMessage(const std::string& text) {
    std::string out = text;
    if (out.size() > hal::kFloatingWindowMaxBytes) {
        out.resize(hal::kFloatingWindowMaxBytes);
        DP_LOG_WARN("display", "floating-window message truncated to 255 bytes");
    }
    const T_DjiReturnCode ret = DjiWidgetFloatingWindow_ShowMessage(out.c_str());
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        DP_LOG_WARN("display", "DjiWidgetFloatingWindow_ShowMessage failed");
    }
}

}  // namespace droppoint::psdk
