// psdk/release_widget.h
//
// Status: unverified — requires hardware.
//
// hal::IRelease that presses ANOTHER payload's widget through the PSDK
// Widget Manager — the only software-actuated release path on the M400
// (docs/CZI-DROP-KITS.md; Widget Manager itself is M400/Linux only, enforced
// by config validation). Target widget comes from aircraft.yaml
// release.widget: {mount_position, index, type: button|switch, fire_value,
// safe_value}.
//
//   Fire()    -> SetWidgetState(fire_value); for a "button" widget a reset
//                to 0 follows 200 ms later on a detached thread, mirroring a
//                real press.
//   SetSafe() -> "switch" widgets: SetWidgetState(safe_value); buttons need
//                no safe action (momentary).
//   IsOperatorActuated() -> false (this backend IS software-actuated)
//   IsResponding()       -> last SetWidgetState call succeeded
#pragma once

#include <atomic>
#include <thread>

#include "hal/i_release.h"
#include "util/config.h"

namespace droppoint::psdk {

class PsdkWidgetRelease : public hal::IRelease {
public:
    explicit PsdkWidgetRelease(const util::config::ReleaseConfig& config);
    ~PsdkWidgetRelease() override;

    PsdkWidgetRelease(const PsdkWidgetRelease&) = delete;
    PsdkWidgetRelease& operator=(const PsdkWidgetRelease&) = delete;

    // DjiWidgetManager_Init — M400/Linux only. Throws std::runtime_error on
    // failure (config validation already guarantees M400).
    void Init();

    void Fire() override;       // single-shot: kSafe -> kFired
    void SetSafe() override;    // idempotent, any state
    hal::ReleaseState GetState() override { return state_; }
    bool IsResponding() override { return responding_; }
    bool IsOperatorActuated() const override { return false; }

private:
    bool SetWidget(std::int32_t value);

    util::config::ReleaseConfig config_;
    bool is_switch_ = false;
    std::atomic<hal::ReleaseState> state_{hal::ReleaseState::kSafe};
    std::atomic<bool> responding_{false};
    std::atomic<bool> reset_pending_{false};
    std::thread reset_thread_;
};

}  // namespace droppoint::psdk
