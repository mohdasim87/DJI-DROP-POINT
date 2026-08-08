// hal/i_telemetry.h
//
// Aircraft telemetry: fused position/attitude/velocity, GNSS/RTK status,
// barometric altitude, and (see docs/OPEN-QUESTIONS.md #2) the hover-
// attitude wind estimate since no PSDK wind topic exists. Implemented by
// src/psdk (real aircraft, via Data Subscription topics) and src/sim
// (scripted/simulated telemetry). core/ never includes this header.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/solution.h"

namespace droppoint::hal {

enum class RtkFixStatus { kNotAvailable, kFloat, kFix };

struct GnssStatus {
    int satellite_count = 0;
    RtkFixStatus rtk_status = RtkFixStatus::kNotAvailable;
    bool rtk_connected = false;
};

// A logical RC button/switch event, synthesised by polling the RC
// subscription topic and mapping specific channel positions to actions, per
// docs/OPEN-QUESTIONS.md #3 (no native RC button binding API exists in
// PSDK). See config/aircraft.yaml `rc_button_map`.
enum class RcButton { kArm, kRelease, kAbort, kSetTarget, kConfirmTarget, kClearTarget };
struct RcButtonEvent {
    RcButton button;
    std::uint64_t timestamp_us;
};

// Implemented by src/psdk (DjiFcSubscription_* topics) and src/sim.
class ITelemetry {
public:
    virtual ~ITelemetry() = default;

    virtual core::AircraftState GetAircraftState() = 0;
    virtual GnssStatus GetGnssStatus() = 0;
    virtual double GetBarometricAltitudeM() = 0;
    virtual std::uint64_t GetTelemetryAgeUs() = 0;  // time since the last update was received

    // No PSDK wind topic exists (docs/OPEN-QUESTIONS.md #2): this always
    // returns the hover-attitude estimate (core::wind::EstimateFromHover),
    // continuously computed and validity-gated by the implementation. A
    // future external wind sensor or PSDK wind topic would populate this
    // directly instead.
    virtual core::WindSample GetWind() = 0;

    // Polls for RC button events since the last call (see RcButtonEvent
    // above for why this is polling-based, not a native callback).
    virtual std::vector<RcButtonEvent> PollRcButtonEvents() = 0;

    // Registers a callback invoked whenever fresh telemetry arrives, for
    // event-driven consumers (the 10 Hz guidance loop still polls on its
    // own timer regardless, this is for logging/dashboard use).
    using UpdateCallback = std::function<void(const core::AircraftState&)>;
    virtual void RegisterUpdateCallback(UpdateCallback callback) = 0;
};

}  // namespace droppoint::hal
