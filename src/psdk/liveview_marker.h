// psdk/liveview_marker.h
//
// Video AI-metadata marker channel via DjiLiveview_SendAiMetaToPilot.
// Only functional on DJI Manifold 3 (docs/PSDK-RESEARCH.md section 6).
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "hal/i_liveview.h"

namespace droppoint::psdk {

class LiveviewMarker : public hal::ILiveview {
public:
    LiveviewMarker();
    ~LiveviewMarker() override;
    void SendMarkers(const std::vector<hal::Marker>& markers) override;

private:
    std::mutex mutex_;
    std::uint32_t sequence_ = 0;
};

}  // namespace droppoint::psdk
