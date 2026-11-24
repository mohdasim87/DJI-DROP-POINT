// hw/logging_audio.h
//
// Log-only hal::IAudio used when there is no audible channel: generic
// Linux builds (the M4T profile's companion computer has no speaker/TTS
// path — the widget speaker is deliberately NOT registered, it would
// advertise hardware that does not exist). Also the fallback on Manifold 3
// before HMS init succeeds.
#pragma once

#include <string>

#include "hal/i_audio.h"
#include "util/logging.h"

namespace droppoint::hw {

class LoggingAudio : public hal::IAudio {
public:
    void Speak(const std::string& phrase) override {
        util::Logger::Instance().Log(util::LogLevel::kInfo, "audio", "speak: " + phrase);
        last_phrase_ = phrase;
    }
    void PlayTone() override {
        util::Logger::Instance().Log(util::LogLevel::kInfo, "audio", "tone");
    }
    bool IsBusy() override { return false; }

    const std::string& LastPhrase() const { return last_phrase_; }

private:
    std::string last_phrase_;
};

}  // namespace droppoint::hw
