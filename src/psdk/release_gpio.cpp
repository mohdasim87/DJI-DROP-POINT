// Status: unverified — requires hardware.
#include "release_gpio.h"

#include <cstdio>
#include <fstream>

#include "util/logging.h"

namespace droppoint::psdk {

namespace {
// Minimal sysfs PWM writer. Real error handling (permission errors,
// missing kernel PWM driver, wrong chip path) surfaces as `responding_ =
// false`, which the startup self-test (spec 15.3) reports as "release GPIO
// responding: NO" rather than crashing the guidance loop.
bool WriteSysfsFile(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f) return false;
    f << value;
    return f.good();
}
}  // namespace

ReleaseGpio::ReleaseGpio(ReleaseGpioConfig config) : config_(std::move(config)) {
    const std::string export_path = config_.pwm_chip_path + "/export";
    exported_ = WriteSysfsFile(export_path, std::to_string(config_.pwm_channel));
    const std::string channel_path =
        config_.pwm_chip_path + "/pwm" + std::to_string(config_.pwm_channel);
    responding_ = WriteSysfsFile(channel_path + "/period", std::to_string(config_.period_ns));
    if (!responding_) {
        DP_LOG_ERROR("release_gpio", "failed to configure PWM channel at " + channel_path +
                                          " — release output cannot be verified as safe");
    }
    SetSafe();  // fail-safe: always start in the safe state (spec 8.4)
}

ReleaseGpio::~ReleaseGpio() {
    SetSafe();
    if (exported_) {
        WriteSysfsFile(config_.pwm_chip_path + "/unexport", std::to_string(config_.pwm_channel));
    }
}

void ReleaseGpio::WritePulseWidthNs(int pulse_ns) {
    const std::string channel_path =
        config_.pwm_chip_path + "/pwm" + std::to_string(config_.pwm_channel);
    responding_ = WriteSysfsFile(channel_path + "/duty_cycle", std::to_string(pulse_ns)) && responding_;
}

void ReleaseGpio::Fire() {
    if (state_ != hal::ReleaseState::kSafe) return;  // single-shot, per hal::IRelease contract
    state_ = hal::ReleaseState::kFiring;
    WritePulseWidthNs(config_.release_pulse_width_ns);
    state_ = hal::ReleaseState::kFired;
}

void ReleaseGpio::SetSafe() {
    WritePulseWidthNs(config_.safe_pulse_width_ns);
    state_ = hal::ReleaseState::kSafe;
}

}  // namespace droppoint::psdk
