// util/config.h
//
// YAML config loading and schema validation for the four files described in
// spec section 10: payloads.yaml, aircraft.yaml, lenses.yaml, system.yaml.
// Validation failures produce a clear, field-specific error rather than a
// generic parse exception, and uncalibrated placeholders are flagged so the
// software can announce that state at runtime (spec 10's closing
// requirement).
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/gating.h"
#include "core/projection.h"
#include "core/solution.h"

namespace droppoint::util::config {

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& what) : std::runtime_error(what) {}
};

enum class GimbalYawSource { kIndependent, kEqualsAircraftYaw };
enum class PsdkLinkMode {
    kOnlyUart,
    kUartAndUsbBulk,
    kUartAndNetwork,
    kOnlyUsbBulk,
    kOnlyNetwork,
};

struct AircraftConfig {
    std::string model;
    double cda_airframe = 0.0;
    double mass_kg = 0.0;
    GimbalYawSource gimbal_yaw_source = GimbalYawSource::kEqualsAircraftYaw;
    bool rangefinder_present = false;
    bool rtk_required = false;
    PsdkLinkMode psdk_link_mode = PsdkLinkMode::kOnlyUart;
    int baud_rate = 921600;
    bool pps_available = false;
};

struct LensProfile {
    std::string aircraft_model;
    int camera_index = 0;
    std::string stream_source;
    core::projection::Intrinsics intrinsics;
    std::string calibration_date;  // empty => uncalibrated placeholder
    bool calibrated = false;
};

struct SystemConfig {
    core::gating::Thresholds gate_thresholds;
    double guidance_loop_hz = 10.0;
    double floating_window_hz = 2.0;
    int monte_carlo_samples = 200;
    double monte_carlo_min_rate_hz = 1.0;
    double video_telemetry_offset_ms = 0.0;
    std::string log_directory = "/var/log/droppoint";
    std::string dem_directory = "/opt/droppoint/data/dem";
    std::string units = "metric";
    double auto_mode_max_solution_divergence_m = 25.0;
};

struct Config {
    AircraftConfig aircraft;
    std::vector<core::PayloadProfile> payloads;
    std::vector<LensProfile> lenses;
    SystemConfig system;
};

AircraftConfig LoadAircraftConfig(const std::string& path);
std::vector<core::PayloadProfile> LoadPayloadsConfig(const std::string& path);
std::vector<LensProfile> LoadLensesConfig(const std::string& path);
SystemConfig LoadSystemConfig(const std::string& path);

// Loads and cross-validates all four files under `config_dir`
// (aircraft.yaml, payloads.yaml, lenses.yaml, system.yaml). Throws
// ConfigError with a specific, actionable message on any problem.
Config LoadAll(const std::string& config_dir);

}  // namespace droppoint::util::config
