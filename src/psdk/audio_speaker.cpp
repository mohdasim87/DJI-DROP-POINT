// psdk/audio_speaker.cpp
//
// TTS / tone countdown via the PSDK widget speaker handler.
// Status: unverified — compiles against real headers, needs hardware to verify.
//
// The PSDK speaker model is: the payload registers a handler. Pilot sends TTS
// audio data (ReceiveTtsData) and control commands (StartPlay/StopPlay) through
// that handler when a widget requests speech. DropPoint also has a local
// Speak() path used by the countdown state machine; on a real aircraft this
// would drive a local audio output (buzzer or companion-computer speaker),
// because the PSDK handler alone does not expose a "say this text" API.
#include "audio_speaker.h"

#include "psdk_util.h"
#include "util/logging.h"

#include "dji_widget.h"

namespace droppoint::psdk {

namespace {

extern "C" T_DjiReturnCode SpeakerGetState(T_DjiWidgetSpeakerState* state) {
    if (!state) return DJI_RETURN_CODE_ERR_PARAM;
    state->state = DJI_WIDGET_SPEAKER_STATE_IDEL;
    state->workMode = DJI_WIDGET_SPEAKER_WORK_MODE_TTS;
    state->playMode = DJI_WIDGET_SPEAKER_PLAY_MODE_SINGLE_PLAY;
    state->volume = 80;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerSetWorkMode(E_DjiWidgetSpeakerWorkMode mode) {
    (void)mode;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerSetPlayMode(E_DjiWidgetSpeakerPlayMode mode) {
    (void)mode;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerSetVolume(uint8_t volume) {
    (void)volume;
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerStartPlay(void) {
    DP_LOG_INFO("audio_speaker", "Pilot requested speaker start");
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerStopPlay(void) {
    DP_LOG_INFO("audio_speaker", "Pilot requested speaker stop");
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerReceiveTtsData(E_DjiWidgetTransmitDataEvent event,
                                                 uint32_t offset, uint8_t* buf, uint16_t size) {
    (void)event;
    (void)offset;
    (void)buf;
    (void)size;
    // In a hardware-verified build this would decode/queue the TTS audio data.
    return DJI_RETURN_CODE_OK;
}

extern "C" T_DjiReturnCode SpeakerReceiveVoiceData(E_DjiWidgetTransmitDataEvent event,
                                                   uint32_t offset, uint8_t* buf, uint16_t size) {
    (void)event;
    (void)offset;
    (void)buf;
    (void)size;
    return DJI_RETURN_CODE_OK;
}

}  // namespace

AudioSpeaker::AudioSpeaker() {
    T_DjiWidgetSpeakerHandler handler{};
    handler.GetSpeakerState = SpeakerGetState;
    handler.SetWorkMode = SpeakerSetWorkMode;
    handler.SetPlayMode = SpeakerSetPlayMode;
    handler.SetVolume = SpeakerSetVolume;
    handler.StartPlay = SpeakerStartPlay;
    handler.StopPlay = SpeakerStopPlay;
    handler.ReceiveTtsData = SpeakerReceiveTtsData;
    handler.ReceiveVoiceData = SpeakerReceiveVoiceData;
    CheckReturn(DjiWidget_RegSpeakerHandler(&handler), "DjiWidget_RegSpeakerHandler");
}

AudioSpeaker::~AudioSpeaker() = default;

void AudioSpeaker::Speak(const std::string& phrase) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_phrase_ = phrase;
    busy_ = true;
    // Assume ~300 ms per word as a rough busy estimate. On real hardware this
    // would track the actual audio playback buffer.
    const std::size_t words = std::max<std::size_t>(1, 1 + phrase.find(' '));
    busy_until_us_ = MonotonicUs() + words * 300000;
    DP_LOG_INFO("audio_speaker", "speak: " + phrase);
}

void AudioSpeaker::PlayTone() {
    std::lock_guard<std::mutex> lock(mutex_);
    busy_ = true;
    busy_until_us_ = MonotonicUs() + 250000;  // 250 ms tone
    DP_LOG_INFO("audio_speaker", "play tone");
}

bool AudioSpeaker::IsBusy() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_ && MonotonicUs() >= busy_until_us_) {
        busy_ = false;
    }
    return busy_;
}

}  // namespace droppoint::psdk
