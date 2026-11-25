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
enum class PsdkPlatform { kGenericLinux, kManifold3 };
enum class ReleaseBackend { kPwmServo, kExternalOperator, kPsdkWidget };
enum class DefaultReleaseMode { kManual, kAuto };

// The `release:` map in aircraft.yaml: how the payload release mechanism is
// driven. pwm_servo is a locally-actuated PWM output DropPoint controls;
// external_operator is a third-party kit (e.g. CZI TH2/TH4) the operator
// triggers in DJI Pilot — auto release is unavailable with that backend;
// psdk_widget presses another payload's drop widget through the PSDK Widget
// Manager (Matrice 400 only, see docs/CZI-DROP-KITS.md).
struct ReleaseConfig {
    ReleaseBackend backend = ReleaseBackend::kPwmServo;
    std::string description;
    // `release.kit`: optional drop-kit catalogue id (util/drop_kits.h, see
    // docs/DROP-KITS.md). When set, LoadAll validates the kit against the
    // aircraft model, this backend and every payload weight; when
    // `description` is empty it is filled from the kit.
    std::string kit;
    double hold_s = 1.5;  // post-fire hold before the output is driven safe

    // PWM servo parameters, under `release.pwm`.
    std::string pwm_chip_path = "/sys/class/pwm/pwmchip0";
    int pwm_channel = 0;
    int period_ns = 20000000;
    int safe_pulse_ns = 1000000;
    int release_pulse_ns = 2000000;

    // Widget Manager target, under `release.widget` (psdk_widget backend
    // only): which widget on the OTHER payload (e.g. the CZI TH4) to set.
    int widget_mount_position = 0;  // E_DjiMountPosition of the drop kit
    int widget_index = 0;
    std::string widget_type = "button";  // "button" | "switch"
    int widget_fire_value = 1;
    int widget_safe_value = 0;
};

// The `auth_chip:` map in aircraft.yaml: the DJI SDK Certified Chip the
// Matrice 4 series requires on the payload side (docs/SETUP-MANUAL.md 5.3).
// It sits on an I2C bus at 0x2A and needs NRST pulsed at power-up.
struct AuthChipConfig {
    bool enabled = false;
    std::string i2c_device = "/dev/i2c-1";
    std::string reset_gpio_chip = "/dev/gpiochip0";
    int reset_gpio_line = 4;
    int reset_pulse_ms = 25;
};

struct AircraftConfig {
    std::string model;
    double cda_airframe = 0.0;
    double mass_kg = 0.0;
    double max_payload_kg = 0.0;      // REQUIRED: max releasable weight carried, kg
    double onboard_equipment_kg = 0.0;  // companion computer, mounts, drop kit, etc.
    bool airframe_calibrated = false;
    GimbalYawSource gimbal_yaw_source = GimbalYawSource::kEqualsAircraftYaw;
    bool rangefinder_present = false;
    bool rtk_required = false;
    PsdkPlatform psdk_platform = PsdkPlatform::kGenericLinux;
    bool advanced_features = false;   // DJI advanced permission features available
    bool liveview_marker = false;
    std::string uart_device = "/dev/ttyUSB0";
    // USB-serial adapter identity reported to the FC (see
    // docs/CZI-DROP-KITS.md: M400 E-Port V2 has no UART pins). Defaults are
    // the ubiquitous FTDI FT232.
    int uart_usb_vid = 0x0403;
    int uart_usb_pid = 0x6001;
    int camera_mount_position = 1;
    PsdkLinkMode psdk_link_mode = PsdkLinkMode::kOnlyUart;
    int baud_rate = 921600;
    bool pps_available = false;
    ReleaseConfig release;
    AuthChipConfig auth_chip;
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
    double ambient_temperature_c = 15.0;
    double min_run_in_speed_mps = 1.0;
    double wind_alpha = 0.14;               // power-law exponent, see core::wind
    double wind_window_s = 10.0;            // `wind:` map: hover-estimator window
    double wind_min_hover_s = 3.0;
    double wind_max_age_s = 300.0;
    bool allow_flat_terrain_fallback = true;
    // terrain.allow_raw_dem: permit raw (untied, EGM96) DEM elevations when
    // no home tie exists. Default false — a raw SRTM height can be tens of
    // metres off the WGS-84 ellipsoid altitude the aircraft reports.
    bool allow_raw_dem = false;
    DefaultReleaseMode default_release_mode = DefaultReleaseMode::kManual;
};

struct Config {
    AircraftConfig aircraft;
    std::vector<core::PayloadProfile> payloads;
    std::vector<LensProfile> lenses;
    SystemConfig system;
};

// Aircraft models this build supports (the allowlist aircraft.yaml's
// `model` is validated against, and the set `custom_pwm_servo` drop kits
// claim compatibility with).
const std::vector<std::string>& KnownAircraftModels();

AircraftConfig LoadAircraftConfig(const std::string& path);
std::vector<core::PayloadProfile> LoadPayloadsConfig(const std::string& path);
std::vector<LensProfile> LoadLensesConfig(const std::string& path);
SystemConfig LoadSystemConfig(const std::string& path);

// Loads and cross-validates all four files under `config_dir`
// (aircraft.yaml, payloads.yaml, lenses.yaml, system.yaml). Throws
// ConfigError with a specific, actionable message on any problem.
Config LoadAll(const std::string& config_dir);

}  // namespace droppoint::util::config
