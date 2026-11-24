// psdk/audio_hms.h
//
// Status: unverified — requires hardware.
//
// hal::IAudio backed by DjiHmsCustomization_AlarmEnhancedCtrl — sound +
// vibration pulses on the RC. Manifold 3 only (the header says so); main
// uses hw::LoggingAudio on every other platform. There is no speaker: no
// widget speaker handler is registered, deliberately — registering one
// advertises a loudspeaker channel this payload does not have.
//
// Pattern mapping (short enough to be fire-and-forget):
//   cue ("three"/"two"/"one", tone)  -> 1 sound pulse
//   "drop"                           -> 3 quick sound+shake pulses
//   "abort"                          -> long sound+shake pulse (interval 800 ms, x2)
//   anything else                    -> 1 sound+shake pulse
#pragma once

#include "hal/i_audio.h"

namespace droppoint::psdk {

class HmsAudio : public hal::IAudio {
public:
    // Calls DjiHmsCustomization_Init(). Throws std::runtime_error on
    // failure — main falls back to LoggingAudio.
    void Init();

    void Speak(const std::string& phrase) override;
    void PlayTone() override;
    bool IsBusy() override { return false; }  // pulses are asynchronous

private:
    void Pulse(int type, int times, int interval_ms);

    bool initialised_ = false;
    bool warned_ = false;  // log failures once, never spam
};

}  // namespace droppoint::psdk
