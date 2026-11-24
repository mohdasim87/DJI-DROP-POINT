// hw/pwm_servo_release.h
//
// Linux sysfs-PWM release backend (replaces psdk/release_gpio.*). Pure file
// I/O — no DJI headers — so the full sequence is unit-testable against a
// temp directory standing in for /sys/class/pwm/pwmchipN.
//
// Construction performs the complete init sequence:
//   export -> period -> duty_cycle = safe pulse -> enable = 1
// (the previous implementation never wrote `enable`, so no pulse ever left
// the pin). Destruction writes the safe pulse again. Every write result
// feeds IsResponding() (spec 15.3 self-test) and WriteLog() records the
// exact file accesses for field debugging.
#pragma once

#include <string>
#include <vector>

#include "hal/i_release.h"

namespace droppoint::hw {

struct PwmServoConfig {
    std::string chip_path = "/sys/class/pwm/pwmchip0";
    int channel = 0;
    int period_ns = 20000000;      // 50 Hz hobby servo period
    int safe_pulse_ns = 1000000;
    int release_pulse_ns = 2000000;
};

class PwmServoRelease : public hal::IRelease {
public:
    // Exports the channel (if needed) and drives it to the safe pulse.
    // Never throws: failures land in IsResponding() == false.
    explicit PwmServoRelease(PwmServoConfig config);
    ~PwmServoRelease() override;

    PwmServoRelease(const PwmServoRelease&) = delete;
    PwmServoRelease& operator=(const PwmServoRelease&) = delete;

    void Fire() override;       // single-shot: kSafe -> kFired
    void SetSafe() override;    // idempotent, any state
    hal::ReleaseState GetState() override { return state_; }
    bool IsResponding() override { return responding_; }

    // "path<-value" entries in write order (self-test/debug output).
    const std::vector<std::string>& WriteLog() const { return write_log_; }

private:
    bool WriteFile(const std::string& path, const std::string& value);
    std::string ChannelDir() const;

    PwmServoConfig config_;
    hal::ReleaseState state_ = hal::ReleaseState::kSafe;
    bool responding_ = false;
    std::vector<std::string> write_log_;
};

}  // namespace droppoint::hw
