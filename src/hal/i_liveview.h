// hal/i_liveview.h
//
// The video AI-metadata marker channel (spec section 9), via
// DjiLiveview_SendAiMetaToPilot, Manifold-3-only per docs/PSDK-RESEARCH.md
// section 6. A null implementation is mandatory, not optional: the system
// must be fully functional (text + audio) without this channel, and the
// struct this maps to has no live numeric-label field (see the research
// doc), so labels here are drawn from a small fixed set, not formatted
// numbers.
#pragma once

#include <vector>

#include "core/solution.h"

namespace droppoint::hal {

enum class MarkerKind { kLatchedTarget, kPredictedImpact, kVirtualCursor };

struct Marker {
    MarkerKind kind;
    bool visible = false;
    double pixel_u = 0.0;
    double pixel_v = 0.0;
};

class ILiveview {
public:
    virtual ~ILiveview() = default;

    // Sends the current set of markers. Implementations that cannot support
    // this channel (null implementation, or any non-Manifold-3 platform)
    // simply do nothing; callers never special-case that possibility.
    virtual void SendMarkers(const std::vector<Marker>& markers) = 0;
};

}  // namespace droppoint::hal
