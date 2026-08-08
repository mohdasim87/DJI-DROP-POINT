#include "config.h"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <filesystem>

#include "core/math_constants.h"

namespace droppoint::util::config {

namespace {

constexpr double kDegToRad = core::kPi / 180.0;

YAML::Node LoadFile(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        throw ConfigError("config file not found: " + path);
    }
    try {
        return YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw ConfigError("failed to parse " + path + ": " + e.what());
    }
}

template <typename T>
T RequireField(const YAML::Node& node, const std::string& field, const std::string& context) {
    if (!node[field]) {
        throw ConfigError(context + ": missing required field '" + field + "'");
    }
    try {
        return node[field].as<T>();
    } catch (const YAML::Exception&) {
        throw ConfigError(context + ": field '" + field + "' has the wrong type");
    }
}

template <typename T>
T OptionalField(const YAML::Node& node, const std::string& field, T default_value) {
    if (!node[field]) return default_value;
    return node[field].as<T>();
}

GimbalYawSource ParseYawSource(const std::string& s, const std::string& context) {
    if (s == "independent") return GimbalYawSource::kIndependent;
    if (s == "equals_aircraft_yaw") return GimbalYawSource::kEqualsAircraftYaw;
    throw ConfigError(context + ": gimbal_yaw_source must be 'independent' or 'equals_aircraft_yaw', got '" + s + "'");
}

PsdkLinkMode ParseLinkMode(const std::string& s, const std::string& context) {
    if (s == "only_uart") return PsdkLinkMode::kOnlyUart;
    if (s == "uart_and_usb_bulk") return PsdkLinkMode::kUartAndUsbBulk;
    if (s == "uart_and_network") return PsdkLinkMode::kUartAndNetwork;
    if (s == "only_usb_bulk") return PsdkLinkMode::kOnlyUsbBulk;
    if (s == "only_network") return PsdkLinkMode::kOnlyNetwork;
    throw ConfigError(context + ": unrecognised psdk_link_mode '" + s + "'");
}

}  // namespace

AircraftConfig LoadAircraftConfig(const std::string& path) {
    const YAML::Node root = LoadFile(path);
    const std::string ctx = "aircraft.yaml";

    AircraftConfig cfg;
    cfg.model = RequireField<std::string>(root, "model", ctx);
    cfg.cda_airframe = RequireField<double>(root, "cda_airframe", ctx);
    cfg.mass_kg = RequireField<double>(root, "mass_kg", ctx);
    cfg.gimbal_yaw_source =
        ParseYawSource(RequireField<std::string>(root, "gimbal_yaw_source", ctx), ctx);
    cfg.rangefinder_present = OptionalField(root, "rangefinder_present", false);
    cfg.rtk_required = OptionalField(root, "rtk_required", false);
    cfg.psdk_link_mode =
        ParseLinkMode(OptionalField<std::string>(root, "psdk_link_mode", "only_uart"), ctx);
    cfg.baud_rate = OptionalField(root, "baud_rate", 921600);
    cfg.pps_available = OptionalField(root, "pps_available", false);

    if (cfg.mass_kg <= 0.0) throw ConfigError(ctx + ": mass_kg must be positive");
    if (cfg.cda_airframe <= 0.0) throw ConfigError(ctx + ": cda_airframe must be positive");
    return cfg;
}

std::vector<core::PayloadProfile> LoadPayloadsConfig(const std::string& path) {
    const YAML::Node root = LoadFile(path);
    const std::string ctx = "payloads.yaml";
    if (!root["payloads"] || !root["payloads"].IsSequence()) {
        throw ConfigError(ctx + ": expected a top-level 'payloads' sequence");
    }

    std::vector<core::PayloadProfile> profiles;
    for (const auto& node : root["payloads"]) {
        const std::string name = RequireField<std::string>(node, "name", ctx);
        const std::string item_ctx = ctx + " (payload '" + name + "')";

        core::PayloadProfile p;
        p.name = name;
        p.mass_kg = RequireField<double>(node, "mass_kg", item_ctx);
        p.cda_free = RequireField<double>(node, "cda_free", item_ctx);
        if (node["cda_deployed"]) p.cda_deployed = node["cda_deployed"].as<double>();
        if (node["deploy_delay_s"]) p.deploy_delay_s = node["deploy_delay_s"].as<double>();
        p.min_release_agl_m = OptionalField(node, "min_release_agl_m", 0.0);
        p.max_release_agl_m = OptionalField(node, "max_release_agl_m", 1e9);
        p.crosswind_limit_mps = OptionalField(node, "crosswind_limit_mps", 1e9);
        p.target_radius_m = OptionalField(node, "target_radius_m", 0.0);
        p.mechanism_delay_s = OptionalField(node, "mechanism_delay_s", 0.0);
        p.calibrated = OptionalField(node, "calibrated", false);
        p.calibration_date = OptionalField<std::string>(node, "calibration_date", "");

        if (p.mass_kg <= 0.0) throw ConfigError(item_ctx + ": mass_kg must be positive");
        if (p.cda_free <= 0.0) throw ConfigError(item_ctx + ": cda_free must be positive");
        if (p.cda_deployed.has_value() != p.deploy_delay_s.has_value()) {
            throw ConfigError(item_ctx + ": cda_deployed and deploy_delay_s must both be set or both omitted");
        }
        profiles.push_back(std::move(p));
    }
    if (profiles.empty()) throw ConfigError(ctx + ": at least one payload profile is required");
    return profiles;
}

std::vector<LensProfile> LoadLensesConfig(const std::string& path) {
    const YAML::Node root = LoadFile(path);
    const std::string ctx = "lenses.yaml";
    if (!root["lenses"] || !root["lenses"].IsSequence()) {
        throw ConfigError(ctx + ": expected a top-level 'lenses' sequence");
    }

    std::vector<LensProfile> lenses;
    for (const auto& node : root["lenses"]) {
        LensProfile lens;
        lens.aircraft_model = RequireField<std::string>(node, "aircraft_model", ctx);
        lens.camera_index = OptionalField(node, "camera_index", 0);
        lens.stream_source = RequireField<std::string>(node, "stream_source", ctx);
        const std::string item_ctx =
            ctx + " (" + lens.aircraft_model + "/" + lens.stream_source + ")";

        core::projection::Intrinsics& intr = lens.intrinsics;
        intr.image_width_px = RequireField<double>(node, "image_width_px", item_ctx);
        intr.image_height_px = RequireField<double>(node, "image_height_px", item_ctx);
        intr.hfov_rad = RequireField<double>(node, "hfov_deg", item_ctx) * kDegToRad;
        intr.vfov_rad = RequireField<double>(node, "vfov_deg", item_ctx) * kDegToRad;
        intr.c_x = OptionalField(node, "principal_point_x_px", intr.image_width_px / 2.0);
        intr.c_y = OptionalField(node, "principal_point_y_px", intr.image_height_px / 2.0);

        if (node["k1"] || node["k2"] || node["k3"] || node["p1"] || node["p2"]) {
            intr.has_distortion = true;
            intr.k1 = OptionalField(node, "k1", 0.0);
            intr.k2 = OptionalField(node, "k2", 0.0);
            intr.k3 = OptionalField(node, "k3", 0.0);
            intr.p1 = OptionalField(node, "p1", 0.0);
            intr.p2 = OptionalField(node, "p2", 0.0);
        }

        lens.calibration_date = OptionalField<std::string>(node, "calibration_date", "");
        lens.calibrated = !lens.calibration_date.empty();
        lenses.push_back(std::move(lens));
    }
    return lenses;
}

SystemConfig LoadSystemConfig(const std::string& path) {
    const YAML::Node root = LoadFile(path);
    const std::string ctx = "system.yaml";

    SystemConfig cfg;
    if (root["gates"]) {
        const YAML::Node g = root["gates"];
        auto& th = cfg.gate_thresholds;
        th.target_max_age_s = OptionalField(g, "target_max_age_s", th.target_max_age_s);
        th.depression_warn_rad = OptionalField(g, "depression_warn_deg", 30.0) * kDegToRad;
        th.depression_refuse_rad = OptionalField(g, "depression_refuse_deg", 15.0) * kDegToRad;
        th.nominal_max_agl_m = OptionalField(g, "nominal_max_agl_m", th.nominal_max_agl_m);
        th.min_gnss_satellites = OptionalField(g, "min_gnss_satellites", th.min_gnss_satellites);
        th.max_telemetry_age_s = OptionalField(g, "max_telemetry_age_s", th.max_telemetry_age_s);
        th.max_acceleration_mps2 = OptionalField(g, "max_acceleration_mps2", th.max_acceleration_mps2);
        th.max_solution_divergence_m =
            OptionalField(g, "max_solution_divergence_m", th.max_solution_divergence_m);
        th.gust_sigma_warn_mps = OptionalField(g, "gust_sigma_warn_mps", th.gust_sigma_warn_mps);
    }

    cfg.guidance_loop_hz = OptionalField(root, "guidance_loop_hz", 10.0);
    cfg.floating_window_hz = OptionalField(root, "floating_window_hz", 2.0);
    cfg.monte_carlo_samples = OptionalField(root, "monte_carlo_samples", 200);
    cfg.monte_carlo_min_rate_hz = OptionalField(root, "monte_carlo_min_rate_hz", 1.0);
    cfg.video_telemetry_offset_ms = OptionalField(root, "video_telemetry_offset_ms", 0.0);
    cfg.log_directory = OptionalField<std::string>(root, "log_directory", cfg.log_directory);
    cfg.dem_directory = OptionalField<std::string>(root, "dem_directory", cfg.dem_directory);
    cfg.units = OptionalField<std::string>(root, "units", cfg.units);
    cfg.auto_mode_max_solution_divergence_m =
        OptionalField(root, "auto_mode_max_solution_divergence_m", cfg.auto_mode_max_solution_divergence_m);

    if (cfg.guidance_loop_hz <= 0.0) throw ConfigError(ctx + ": guidance_loop_hz must be positive");
    return cfg;
}

Config LoadAll(const std::string& config_dir) {
    namespace fs = std::filesystem;
    Config cfg;
    cfg.aircraft = LoadAircraftConfig((fs::path(config_dir) / "aircraft.yaml").string());
    cfg.payloads = LoadPayloadsConfig((fs::path(config_dir) / "payloads.yaml").string());
    cfg.lenses = LoadLensesConfig((fs::path(config_dir) / "lenses.yaml").string());
    cfg.system = LoadSystemConfig((fs::path(config_dir) / "system.yaml").string());

    bool found_matching_lens = false;
    for (const auto& lens : cfg.lenses) {
        if (lens.aircraft_model == cfg.aircraft.model) found_matching_lens = true;
    }
    if (!found_matching_lens) {
        throw ConfigError("lenses.yaml: no lens profile found for aircraft model '" + cfg.aircraft.model +
                           "' declared in aircraft.yaml");
    }
    return cfg;
}

}  // namespace droppoint::util::config
