// sim/sim_hal.h
//
// Simulator implementations of every HAL interface, backed by a shared
// SimWorld. This is what `--sim <scenario>` wires into app/ instead of
// src/psdk, letting the entire application run end to end with no
// hardware, per spec section 12/17.3.
#pragma once

#include <memory>

#include "core/raycast.h"
#include "core/terrain.h"
#include "hal/i_audio.h"
#include "hal/i_display.h"
#include "hal/i_gimbal.h"
#include "hal/i_liveview.h"
#include "hal/i_rangefinder.h"
#include "hal/i_release.h"
#include "hal/i_target_source.h"
#include "hal/i_telemetry.h"
#include "sim_world.h"

namespace droppoint::sim {

class SimTelemetry : public hal::ITelemetry {
public:
    explicit SimTelemetry(std::shared_ptr<SimWorld> world) : world_(std::move(world)) {}

    core::AircraftState GetAircraftState() override;
    hal::GnssStatus GetGnssStatus() override;
    double GetBarometricAltitudeM() override;
    std::uint64_t GetTelemetryAgeUs() override;
    core::WindSample GetWind() override;
    std::vector<hal::RcButtonEvent> PollRcButtonEvents() override;
    void RegisterUpdateCallback(UpdateCallback callback) override { callback_ = std::move(callback); }

    // Test/dashboard hook: injects a synthetic button press for the next poll.
    void InjectButton(hal::RcButton button);

private:
    std::shared_ptr<SimWorld> world_;
    core::AircraftState last_valid_;
    std::uint64_t last_update_us_ = 0;
    UpdateCallback callback_;
    std::vector<hal::RcButtonEvent> pending_buttons_;
};

class SimGimbal : public hal::IGimbal {
public:
    explicit SimGimbal(std::shared_ptr<SimWorld> world) : world_(std::move(world)) {}
    core::GimbalState GetAttitude() override { return world_->GimbalAttitude(); }
    void SlewRate(double, double) override {}
    void Reset() override {}

private:
    std::shared_ptr<SimWorld> world_;
};

class SimRangefinder : public hal::IRangefinder {
public:
    explicit SimRangefinder(std::shared_ptr<SimWorld> world) : world_(std::move(world)) {}
    std::optional<hal::RangefinderReading> GetLatestReading() override;
    void RequestMeasurement() override { measurement_requested_ = true; }

private:
    std::shared_ptr<SimWorld> world_;
    bool measurement_requested_ = false;
};

class SimDisplay : public hal::IDisplay {
public:
    void ShowMessage(const std::string& text) override { last_message_ = text; }
    const std::string& LastMessage() const { return last_message_; }

private:
    std::string last_message_;
};

class SimAudio : public hal::IAudio {
public:
    void Speak(const std::string& phrase) override { last_phrase_ = phrase; ++speak_count_; }
    void PlayTone() override { ++tone_count_; }
    bool IsBusy() override { return false; }
    const std::string& LastPhrase() const { return last_phrase_; }
    int SpeakCount() const { return speak_count_; }

private:
    std::string last_phrase_;
    int speak_count_ = 0;
    int tone_count_ = 0;
};

// The safety-critical fake: mirrors the real hardware contract exactly
// (single-shot Fire, SetSafe reachable/idempotent from any state, defaults
// safe on construction) so release-safety tests exercise real semantics.
class SimRelease : public hal::IRelease {
public:
    SimRelease() { SetSafe(); }
    void Fire() override;
    void SetSafe() override;
    hal::ReleaseState GetState() override { return state_; }
    bool IsResponding() override { return true; }
    int FireCount() const { return fire_count_; }

private:
    hal::ReleaseState state_ = hal::ReleaseState::kSafe;
    int fire_count_ = 0;
};

class SimLiveview : public hal::ILiveview {
public:
    void SendMarkers(const std::vector<hal::Marker>& markers) override { last_markers_ = markers; }
    const std::vector<hal::Marker>& LastMarkers() const { return last_markers_; }

private:
    std::vector<hal::Marker> last_markers_;
};

// Source B (gimbal crosshair): always available. Fires the simulated
// rangefinder and raycasts the centre pixel through the current gimbal
// attitude and a flat/sloped simulated DEM.
class SimCrosshairTargetSource : public hal::ITargetSource {
public:
    SimCrosshairTargetSource(std::shared_ptr<SimWorld> world, core::projection::Intrinsics intrinsics)
        : world_(std::move(world)), intrinsics_(intrinsics) {}

    std::string Name() const override { return "gimbal_crosshair"; }
    bool IsAvailable() const override { return true; }
    std::optional<hal::DesignationOutcome> TryDesignate(const hal::DesignationRequest& request) override;

private:
    std::shared_ptr<SimWorld> world_;
    core::projection::Intrinsics intrinsics_;
};

}  // namespace droppoint::sim
