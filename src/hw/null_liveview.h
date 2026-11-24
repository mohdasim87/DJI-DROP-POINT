// hw/null_liveview.h
//
// The mandatory null hal::ILiveview (see hal/i_liveview.h: the system must
// be fully functional without the marker channel). Used on every build —
// liveview AI/AR markers are Manifold-3-only advanced features and are not
// wired in this build.
#pragma once

#include <vector>

#include "hal/i_liveview.h"

namespace droppoint::hw {

class NullLiveview : public hal::ILiveview {
public:
    void SendMarkers(const std::vector<hal::Marker>& markers) override {
        (void)markers;  // no liveview channel in this build
    }
};

}  // namespace droppoint::hw
