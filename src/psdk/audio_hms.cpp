// psdk/audio_hms.cpp — see header. Status: unverified — requires hardware.
#include "audio_hms.h"

#include <dji_typedef.h>
#include <dji_hms_customization.h>

#include <stdexcept>

#include "util/logging.h"

namespace droppoint::psdk {

void HmsAudio::Init() {
    const T_DjiReturnCode ret = DjiHmsCustomization_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiHmsCustomization_Init failed — enhanced alarms are "
                                 "Manifold 3 only");
    }
    initialised_ = true;
}

void HmsAudio::Pulse(int type, int times, int interval_ms) {
    if (!initialised_) return;
    T_DjiHmsAlarmEnhancedSetting setting{};
    setting.type = static_cast<E_DjiHmsAlarmEnhancedType>(type);
    setting.times = static_cast<int8_t>(times);
    setting.interval = static_cast<int16_t>(interval_ms);
    const T_DjiReturnCode ret =
        DjiHmsCustomization_AlarmEnhancedCtrl(DJI_HMS_ALARM_ENHANCED_ACTION_START, setting);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS && !warned_) {
        warned_ = true;
        DP_LOG_WARN("audio", "DjiHmsCustomization_AlarmEnhancedCtrl failed — audio cues "
                             "will be silent for the rest of this run");
    }
}

void HmsAudio::Speak(const std::string& phrase) {
    if (phrase == "drop") {
        Pulse(DJI_HMS_ALARM_ENHANCED_PLAY_SOUND_AND_SHAKE_MOTOR, 3, 150);
    } else if (phrase == "abort") {
        Pulse(DJI_HMS_ALARM_ENHANCED_PLAY_SOUND_AND_SHAKE_MOTOR, 2, 800);
    } else {
        // "three", "two", "one", "auto armed", ... — single alert pulse.
        Pulse(DJI_HMS_ALARM_ENHANCED_PLAY_SOUND, 1, 0);
    }
}

void HmsAudio::PlayTone() { Pulse(DJI_HMS_ALARM_ENHANCED_PLAY_SOUND, 1, 0); }

}  // namespace droppoint::psdk
