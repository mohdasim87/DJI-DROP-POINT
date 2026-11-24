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
    // Airframe parameters used to emulate hover tilt (lean-into-wind) in the
    // reported attitude — the same physics the wind estimator inverts. The
    // drag balance uses the TOTAL airborne mass (airframe + carried
    // payload), so the driver must keep payload_mass_kg in sync with the
    // payload profile it hands the loop.
    double aircraft_mass_kg = 6.3;
    double payload_mass_kg = 1.5;
    double cda_airframe = 0.45;
    // Height AGL the truth wind power law is referenced to, so the truth
    // profile and the estimator's model agree nominally (wind-shear stays
    // deliberately mismatched).
    double wind_reference_height_m = 60.0;
    // Latched-target offset applied by the mis-designated-target scenario:
    // the designation lands here relative to the point the pilot actually
    // aims for, so a correct run-in can never open the release corridor.
    double misdesignated_offset_e_m = 15.0;
    double misdesignated_offset_n_m = 0.0;
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
