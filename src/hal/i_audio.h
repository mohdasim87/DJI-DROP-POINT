// hal/i_audio.h
//
// TTS/tone countdown channel (spec section 9), via DjiWidget_RegSpeakerHandler
// on real hardware (docs/PSDK-RESEARCH.md section 4).
#pragma once

#include <string>

namespace droppoint::hal {

class IAudio {
public:
    virtual ~IAudio() = default;

    // Speaks a short phrase via TTS ("three", "two", "one", "drop", "abort",
    // "auto armed").
    virtual void Speak(const std::string& phrase) = 0;

    // Plays a short tone (the T-5.0 cue), distinct from TTS so it can be
    // implemented as a pre-rendered clip if TTS latency is a concern.
    virtual void PlayTone() = 0;

    virtual bool IsBusy() = 0;  // true while a phrase/tone is still playing
};

}  // namespace droppoint::hal
