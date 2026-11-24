// hw/pwm_servo_release.cpp
#include "pwm_servo_release.h"

#include <filesystem>
#include <fstream>

namespace droppoint::hw {

PwmServoRelease::PwmServoRelease(PwmServoConfig config) : config_(std::move(config)) {
    // 1. Export the channel. A failure is tolerated when the channel
    //    directory already exists (re-export returns EBUSY on sysfs).
    WriteFile(config_.chip_path + "/export", std::to_string(config_.channel));
    if (!std::filesystem::is_directory(ChannelDir())) {
        responding_ = false;
        return;
    }
    // 2-4. Period, safe duty, then enable — order matters: on sysfs a
    // duty_cycle write can be rejected while the channel is disabled and an
    // invalid period is still in effect.
    responding_ = WriteFile(ChannelDir() + "/period", std::to_string(config_.period_ns));
    responding_ = WriteFile(ChannelDir() + "/duty_cycle", std::to_string(config_.safe_pulse_ns)) &&
                  responding_;
    responding_ = WriteFile(ChannelDir() + "/enable", "1") && responding_;
    state_ = hal::ReleaseState::kSafe;
}

PwmServoRelease::~PwmServoRelease() { SetSafe(); }

void PwmServoRelease::Fire() {
    if (state_ != hal::ReleaseState::kSafe) return;  // single-shot
    responding_ = WriteFile(ChannelDir() + "/duty_cycle", std::to_string(config_.release_pulse_ns));
    state_ = hal::ReleaseState::kFired;
}

void PwmServoRelease::SetSafe() {
    // Only attempt I/O once the channel actually exists; before a
    // successful init there is nothing to write to.
    if (!std::filesystem::is_directory(ChannelDir())) return;
    responding_ = WriteFile(ChannelDir() + "/duty_cycle", std::to_string(config_.safe_pulse_ns));
    state_ = hal::ReleaseState::kSafe;
}

std::string PwmServoRelease::ChannelDir() const {
    return config_.chip_path + "/pwm" + std::to_string(config_.channel);
}

bool PwmServoRelease::WriteFile(const std::string& path, const std::string& value) {
    write_log_.push_back(path + " <- " + value);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << value << '\n';
    out.close();
    return static_cast<bool>(out);
}

}  // namespace droppoint::hw
