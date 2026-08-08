// sim/scenario.h
//
// Simulator scenarios, spec section 12. Exposed on the main binary via
// `--sim <scenario>`. Each scenario configures the SimWorld's flight
// profile, wind model, and any fault injection (GNSS/RTK loss, telemetry
// dropout, gimbal slew, mis-designated target).
#pragma once

#include <string>

namespace droppoint::sim {

enum class ScenarioType {
    kSteadyHover,
    kStraightRunIn,
    kCrosswind3ms,
    kCrosswind5ms,
    kCrosswind8ms,
    kGustyWind,
    kWindShear,
    kSlopingTerrain,
    kGnssDegradation,
    kRtkLoss,
    kTelemetryDropout,
    kGimbalSlew,
    kMisdesignatedTarget,
};

struct ScenarioConfig {
    ScenarioType type = ScenarioType::kSteadyHover;
    double run_in_heading_deg = 0.0;
    double run_in_speed_mps = 5.0;
    double wind_mean_mps = 0.0;
    double wind_from_deg = 270.0;
    double wind_gust_band_hz = 0.2;
    double wind_gust_amplitude_mps = 0.0;
    double terrain_slope_deg = 0.0;
    // Realistic timestamps and latency: the simulator never delivers
    // perfectly synchronous data, per spec 12's closing paragraph.
    double video_telemetry_offset_ms = 40.0;
    double telemetry_jitter_ms = 5.0;
    double dropout_start_s = -1.0;   // negative = never
    double dropout_duration_s = 0.0;
};

// Parses a `--sim <name>` argument into a ScenarioConfig with sensible
// per-scenario defaults. Throws std::invalid_argument on an unknown name.
ScenarioConfig ParseScenarioName(const std::string& name);
std::string ScenarioName(ScenarioType type);

}  // namespace droppoint::sim
