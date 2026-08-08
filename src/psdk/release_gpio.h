// psdk/release_gpio.h
//
// Status: unverified — requires hardware.
//
// Drives the payload release servo via Linux sysfs PWM, entirely outside
// PSDK (see docs/OPEN-QUESTIONS.md #4: PSDK provides no GPIO/PWM API at
// all). This is the only hal::IRelease implementation that talks to real
// hardware; sim::SimRelease is its test double.
//
// Pulse widths and the sysfs chip/channel numbers are placeholders pending
// the exact companion-computer/servo pairing (docs/HARDWARE.md); they are
// read from payloads.yaml / aircraft.yaml, never hardcoded, per spec
// section 10.
#pragma once

#include <string>

#include "hal/i_release.h"

namespace droppoint::psdk {

struct ReleaseGpioConfig {
    // e.g. "/sys/class/pwm/pwmchip0"
    std::string pwm_chip_path;
    int pwm_channel = 0;
    int safe_pulse_width_ns = 1000000;    // TODO(hardware): calibrate against the actual servo
    int release_pulse_width_ns = 2000000; // TODO(hardware): calibrate against the actual servo
    int period_ns = 20000000;             // 50 Hz, standard hobby servo period
};

// Status: unverified — requires hardware. Constructing this class opens the
// sysfs PWM channel and immediately writes the safe pulse width, per the
// fail-safe requirement (spec 8.4) that the release output defaults safe on
// startup.
class ReleaseGpio : public hal::IRelease {
public:
    explicit ReleaseGpio(ReleaseGpioConfig config);
    ~ReleaseGpio() override;

    void Fire() override;
    void SetSafe() override;
    hal::ReleaseState GetState() override { return state_; }
    bool IsResponding() override { return responding_; }

private:
    void WritePulseWidthNs(int pulse_ns);

    ReleaseGpioConfig config_;
    hal::ReleaseState state_ = hal::ReleaseState::kSafe;
    bool responding_ = false;
    bool exported_ = false;
};

}  // namespace droppoint::psdk
