// psdk/liveview_marker.cpp
//
// Video AI-metadata marker channel via DjiLiveview_SendAiMetaToPilot.
// Status: unverified — Manifold 3 only, compiles against real headers on Linux.
#include "liveview_marker.h"

#include "util/logging.h"

#ifdef __linux__
#include <cstring>
#include "psdk_util.h"
#include "dji_liveview.h"
#endif

namespace droppoint::psdk {

LiveviewMarker::LiveviewMarker() = default;

LiveviewMarker::~LiveviewMarker() = default;

void LiveviewMarker::SendMarkers(const std::vector<hal::Marker>& markers) {
#ifdef __linux__
    // Build a single metadata packet containing up to one box. The PSDK struct
    // uses a trailing flexible array of length 1, so we allocate one extra box.
    constexpr std::size_t kPayloadSize = sizeof(T_DjiLiveViewStandardMetaData) +
                                         sizeof(T_DjiLiveViewBoundingBox);
    alignas(alignof(T_DjiLiveViewStandardMetaData)) uint8_t payload[kPayloadSize] = {};

    auto* meta = reinterpret_cast<T_DjiLiveViewStandardMetaData*>(payload);
    meta->boxCount = 0;

    for (const auto& marker : markers) {
        if (!marker.visible) continue;

        T_DjiLiveViewBoundingBox box{};
        box.id = 0;
        box.type = static_cast<uint8_t>(marker.kind);
        box.state = 1;  // active
        box.box.cx = static_cast<uint16_t>(marker.pixel_u);
        box.box.cy = static_cast<uint16_t>(marker.pixel_v);
        box.box.w = 16;
        box.box.h = 16;
        box.box.distance = 0;

        // The struct has a trailing array of size 1; we can safely write one.
        meta->boxData[0] = box;
        meta->boxCount = 1;
        break;  // one box per packet to avoid overflowing the flexible array
    }

    const T_DjiReturnCode code = DjiLiveview_SendAiMetaToPilot(meta);
    if (code != DJI_RETURN_CODE_OK) {
        DP_LOG_DEBUG("liveview_marker",
                   "DjiLiveview_SendAiMetaToPilot failed: 0x" + std::to_string(code));
    }
#else
    (void)markers;
    DP_LOG_DEBUG("liveview_marker", "liveview markers not supported on this platform");
#endif
}

}  // namespace droppoint::psdk
