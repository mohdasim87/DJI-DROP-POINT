#include "scenario.h"

#include <stdexcept>
#include <unordered_map>

namespace droppoint::sim {

namespace {
const std::unordered_map<std::string, ScenarioType>& NameMap() {
    static const std::unordered_map<std::string, ScenarioType> map = {
        {"hover", ScenarioType::kSteadyHover},
        {"run-in", ScenarioType::kStraightRunIn},
        {"crosswind-3ms", ScenarioType::kCrosswind3ms},
        {"crosswind-5ms", ScenarioType::kCrosswind5ms},
        {"crosswind-8ms", ScenarioType::kCrosswind8ms},
        {"gusty-wind", ScenarioType::kGustyWind},
        {"wind-shear", ScenarioType::kWindShear},
        {"sloping-terrain", ScenarioType::kSlopingTerrain},
        {"gnss-degradation", ScenarioType::kGnssDegradation},
        {"rtk-loss", ScenarioType::kRtkLoss},
        {"telemetry-dropout", ScenarioType::kTelemetryDropout},
        {"gimbal-slew", ScenarioType::kGimbalSlew},
        {"mis-designated-target", ScenarioType::kMisdesignatedTarget},
    };
    return map;
}
}  // namespace

ScenarioConfig ParseScenarioName(const std::string& name) {
    const auto& map = NameMap();
    const auto it = map.find(name);
    if (it == map.end()) {
        throw std::invalid_argument("unknown --sim scenario: '" + name + "'");
    }

    ScenarioConfig cfg;
    cfg.type = it->second;
    switch (cfg.type) {
        case ScenarioType::kSteadyHover:
            cfg.run_in_speed_mps = 0.0;
            break;
        case ScenarioType::kStraightRunIn:
            cfg.run_in_speed_mps = 5.0;
            break;
        case ScenarioType::kCrosswind3ms:
            cfg.wind_mean_mps = 3.0;
            break;
        case ScenarioType::kCrosswind5ms:
            cfg.wind_mean_mps = 5.0;
            break;
        case ScenarioType::kCrosswind8ms:
            cfg.wind_mean_mps = 8.0;
            break;
        case ScenarioType::kGustyWind:
            cfg.wind_mean_mps = 5.0;
            cfg.wind_gust_amplitude_mps = 3.0;
            cfg.wind_gust_band_hz = 0.3;
            break;
        case ScenarioType::kWindShear:
            cfg.wind_mean_mps = 6.0;
            break;
        case ScenarioType::kSlopingTerrain:
            cfg.terrain_slope_deg = 8.0;
            break;
        case ScenarioType::kGnssDegradation:
            break;
        case ScenarioType::kRtkLoss:
            break;
        case ScenarioType::kTelemetryDropout:
            // Late enough that the scripted pilot is armed and on the
            // run-in when the link drops (arm lands ~39 s, release ~42 s
            // on the scripted timeline), and long enough that the abort
            // cannot be outrun by the remaining flight time.
            cfg.dropout_start_s = 39.5;
            cfg.dropout_duration_s = 5.0;
            break;
        case ScenarioType::kGimbalSlew:
            break;
        case ScenarioType::kMisdesignatedTarget:
            break;
    }
    return cfg;
}

std::string ScenarioName(ScenarioType type) {
    for (const auto& [name, t] : NameMap()) {
        if (t == type) return name;
    }
    return "unknown";
}

}  // namespace droppoint::sim
