// hal/i_target_source.h
//
// Unifies the three designation sources from spec section 3 behind one
// interface, in priority order:
//   A. Pilot Pin Point       - NOT IMPLEMENTABLE with public PSDK today,
//                              see docs/OPEN-QUESTIONS.md #1. Interface
//                              exists so it can slot in later.
//   B. Gimbal crosshair      - always implemented, the guaranteed path.
//   C. Virtual cursor        - off-axis pick via the shared raycast solver.
//
// app/mission_state.* holds a priority-ordered list of ITargetSource and
// uses the first one that reports availability, per spec 3.
#pragma once

#include <optional>
#include <string>

#include "core/solution.h"

namespace droppoint::hal {

struct DesignationRequest {
    bool requested = false;   // operator pressed SET TARGET this cycle
    std::uint64_t timestamp_us = 0;
};

struct DesignationOutcome {
    bool succeeded = false;
    core::Target target;
    std::string failure_reason;  // populated when !succeeded, e.g. "horizon", "no DEM tile"
};

class ITargetSource {
public:
    virtual ~ITargetSource() = default;

    // Human-readable identifier for logs/UI ("pilot_pinpoint", "gimbal_crosshair",
    // "virtual_cursor").
    virtual std::string Name() const = 0;

    // Whether this source is currently usable (e.g. Pilot Pin Point returns
    // false unconditionally today; virtual cursor requires Source C widgets
    // to be active).
    virtual bool IsAvailable() const = 0;

    // Attempts a designation this cycle given a SET TARGET press. Returns
    // std::nullopt if no attempt was made (not requested, or not
    // available); returns a DesignationOutcome (success or failure with
    // reason) if an attempt was made.
    virtual std::optional<DesignationOutcome> TryDesignate(const DesignationRequest& request) = 0;
};

}  // namespace droppoint::hal
