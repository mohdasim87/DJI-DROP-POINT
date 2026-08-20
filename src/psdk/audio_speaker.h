// psdk/audio_speaker.h
//
// TTS / tone countdown via the PSDK widget speaker handler.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "hal/i_audio.h"

namespace droppoint::psdk {

class AudioSpeaker : public hal::IAudio {
public:
    AudioSpeaker();
    ~AudioSpeaker() override;

    void Speak(const std::string& phrase) override;
    void PlayTone() override;
    bool IsBusy() override;

private:
    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    std::string last_phrase_;
    std::uint64_t busy_until_us_ = 0;
};

}  // namespace droppoint::psdk
