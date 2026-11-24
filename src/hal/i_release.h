// hal/i_release.h
//
// The payload release mechanism. PSDK provides no GPIO/PWM API
// (docs/OPEN-QUESTIONS.md #4): the real implementation drives OS-level
// GPIO/PWM directly (Linux sysfs or an I2C PWM controller), entirely
// outside PSDK. This interface's contract is the safety-critical one in
// the whole system: Fire() is single-shot, SetSafe() must be reachable and
// idempotent from any state, and the implementation must default to the
// safe state on construction/crash/watchdog per spec 8.4.
#pragma once

namespace droppoint::hal {

enum class ReleaseState { kSafe, kFiring, kFired };

class IRelease {
public:
    virtual ~IRelease() = default;

    // Actuates the release mechanism once. Implementations must treat this
    // as single-shot: a second call before SetSafe()+re-arm is a no-op, not
    // a second release.
    virtual void Fire() = 0;

    // Immediately drives the mechanism to its safe (non-releasing) state.
    // Must be safe to call from any state, repeatedly, including before
    // Init() completes if possible at the hardware level.
    virtual void SetSafe() = 0;

    virtual ReleaseState GetState() = 0;

    // True if the underlying I/O (GPIO line, PWM channel) responded to the
    // last command; false surfaces as a startup self-test failure per spec
    // 15.3.
    virtual bool IsResponding() = 0;

    // True when the release hardware is a third-party kit the OPERATOR
    // triggers (e.g. a CZI drop kit fired from DJI Pilot) rather than
    // something DropPoint can actuate itself. Fire() on such a backend only
    // records the intended instant; auto release is unavailable.
    virtual bool IsOperatorActuated() const { return false; }
};

}  // namespace droppoint::hal
