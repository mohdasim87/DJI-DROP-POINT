// psdk/main_psdk.cpp
//
// Real aircraft entry point (Linux only). Status: unverified — requires
// hardware; the selftest/--no-link paths are what CI and the install test
// exercise.
//
// Usage:
//   droppoint [--config <dir>] [--credentials <file>] [--verbose]
//   droppoint --selftest [--no-link] [same path flags]
//   droppoint --probe-widgets <mount position> [same path flags]
//   droppoint --version
//
// Path resolution (a Manifold 3 app starts with NO arguments, cwd = the
// app directory):
//   config      --config | ./config | <exe>/../config | /opt/droppoint/config
//   credentials --credentials | <config>/credentials.yaml | /etc/droppoint/credentials.yaml
//   log dir     system.yaml log_directory, falling back to ./data/logs when unwritable
//   widgets     <config>/../assets/widget | /opt/droppoint/assets/widget

#include <dji_aircraft_info.h>
#include <dji_core.h>
#include <dji_typedef.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/crosshair_target_source.h"
#include "app/external_operator_release.h"
#include "app/guidance_loop.h"
#include "core/geodesy.h"
#include "core/solution.h"
#include "hal/i_release.h"
#include "hw/conversions.h"
#include "hw/logging_audio.h"
#include "hw/null_liveview.h"
#include "hw/operator_inputs.h"
#include "hw/pwm_servo_release.h"
#include "hw/sd_notify.h"
#include "hw/terrain_provider.h"
#include "hw/widget_map.h"
#include "hw/widget_ui.h"
#include "psdk/app_registration.h"
#include "psdk/audio_hms.h"
#include "psdk/display_floating_window.h"
#include "psdk/gimbal_psdk.h"
#include "psdk/hal_uart.h"
#include "psdk/platform_linux.h"
#include "psdk/probe_widgets.h"
#include "psdk/rangefinder_camera_manager.h"
#include "psdk/release_widget.h"
#include "hw/selftest_report.h"
#include "psdk/telemetry_subscription.h"
#include "psdk/widget_bridge.h"
#include "util/config.h"
#include "util/drop_log.h"
#include "util/logging.h"
#include "util/version.h"

namespace {

namespace fs = std::filesystem;
using droppoint::util::config::AircraftConfig;
using droppoint::util::config::Config;

std::atomic<bool> g_running{true};
droppoint::hal::IRelease* g_release_for_shutdown = nullptr;

void SignalHandler(int) { g_running = false; }

// Last-ditch safety: drive the release to safe no matter how we die.
void SafeShutdown() {
    droppoint::hw::SdNotifyStopping();
    if (g_release_for_shutdown != nullptr) {
        g_release_for_shutdown->SetSafe();
    }
}

void TerminateHandler() {
    SafeShutdown();
    std::fprintf(stderr, "droppoint: std::terminate — release driven safe\n");
    std::abort();
}

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void Usage() {
    std::printf(
        "droppoint %s (%s)\n"
        "  droppoint [--config <dir>] [--credentials <file>] [--verbose]\n"
        "  droppoint --selftest [--no-link] [path flags]\n"
        "  droppoint --probe-widgets <mount position> [path flags]\n"
        "  droppoint --version\n",
        droppoint::util::kVersionString, droppoint::util::kBuildVersion);
}

bool DirExists(const fs::path& p) { return std::filesystem::is_directory(p); }

// A config dir counts as found when aircraft.yaml is present.
std::string ResolveConfigDir(const std::string& explicit_dir, const fs::path& exe_dir) {
    std::vector<fs::path> candidates;
    if (!explicit_dir.empty()) candidates.emplace_back(explicit_dir);
    candidates.emplace_back("./config");
    candidates.emplace_back(exe_dir / ".." / "config");
    candidates.emplace_back("/opt/droppoint/config");
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c / "aircraft.yaml")) {
            return c.lexically_normal().string();
        }
    }
    if (!explicit_dir.empty()) return explicit_dir;  // report it verbatim in the error
    throw std::runtime_error(
        "no config directory found (searched --config, ./config, <exe>/../config, "
        "/opt/droppoint/config)");
}

// Writes a probe file to test writability; returns true on success.
bool DirIsWritable(const fs::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const fs::path probe = dir / ".write_test";
    FILE* f = std::fopen(probe.string().c_str(), "w");
    if (f == nullptr) return false;
    std::fclose(f);
    std::filesystem::remove(probe, ec);
    return true;
}

T_DjiUserInfo MakeUserInfo(const droppoint::psdk::Credentials& creds) {
    T_DjiUserInfo info{};
    auto copy = [](char* dst, std::size_t cap, const std::string& src, const char* field) {
        if (src.size() >= cap) {
            throw std::runtime_error(std::string("credentials field '") + field +
                                     "' is longer than the PSDK struct allows");
        }
        std::memcpy(dst, src.c_str(), src.size());
    };
    copy(info.appName, sizeof(info.appName), creds.app_name, "app_name");
    copy(info.appId, sizeof(info.appId), creds.app_id, "app_id");
    copy(info.appKey, sizeof(info.appKey), creds.app_key, "app_key");
    copy(info.appLicense, sizeof(info.appLicense), creds.app_license, "app_license");
    copy(info.developerAccount, sizeof(info.developerAccount), creds.developer_account,
         "developer_account");
    copy(info.baudRate, sizeof(info.baudRate), creds.baud_rate, "baud_rate");
    return info;
}

void CheckDji(T_DjiReturnCode code, const char* what) {
    if (code != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (DJI error 0x" +
                                 std::to_string(static_cast<unsigned long long>(code)) + ")");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string explicit_config;
    std::string explicit_credentials;
    bool selftest = false;
    bool no_link = false;
    bool verbose = false;
    int probe_position = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            explicit_config = argv[++i];
        } else if (arg == "--credentials" && i + 1 < argc) {
            explicit_credentials = argv[++i];
        } else if (arg == "--selftest") {
            selftest = true;
        } else if (arg == "--no-link") {
            no_link = true;
        } else if (arg == "--probe-widgets" && i + 1 < argc) {
            probe_position = std::atoi(argv[++i]);
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--version") {
            std::printf("droppoint %s (build %s)\n", droppoint::util::kVersionString,
                        droppoint::util::kBuildVersion);
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            Usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            Usage();
            return 2;
        }
    }
    (void)verbose;

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::atexit(SafeShutdown);
    std::set_terminate(TerminateHandler);

    const fs::path exe_dir = fs::absolute(argv[0]).parent_path();

    try {
        // ---- config ---------------------------------------------------------
        const std::string config_dir = ResolveConfigDir(explicit_config, exe_dir);
        DP_LOG_INFO("main", std::string("droppoint build ") + droppoint::util::kBuildVersion +
                                " starting; config dir: " + config_dir);
        const Config cfg = droppoint::util::config::LoadAll(config_dir);

        // ---- log directory (fall back to ./data/logs, DPK layout) -----------
        std::string log_dir = cfg.system.log_directory;
        if (!DirIsWritable(log_dir)) {
            log_dir = "./data/logs";
            if (!DirIsWritable(log_dir)) {
                log_dir = "/tmp/droppoint-logs";
                std::filesystem::create_directories(log_dir);
            }
            DP_LOG_WARN("main", "system log_directory not writable; using " + log_dir);
        }
        DP_LOG_INFO("main", "log directory: " + log_dir);

        // ---- credentials ----------------------------------------------------
        std::vector<fs::path> cred_candidates;
        if (!explicit_credentials.empty()) cred_candidates.emplace_back(explicit_credentials);
        cred_candidates.emplace_back(fs::path(config_dir) / "credentials.yaml");
        cred_candidates.emplace_back("/etc/droppoint/credentials.yaml");
        std::string credentials_path;
        for (const auto& c : cred_candidates) {
            if (std::filesystem::exists(c)) {
                credentials_path = c.string();
                break;
            }
        }
        std::optional<droppoint::psdk::Credentials> creds;
        bool creds_parse_error = false;
        if (!credentials_path.empty()) {
            try {
                creds = droppoint::psdk::LoadCredentials(credentials_path);
                DP_LOG_INFO("main", "credentials loaded from " + credentials_path);
            } catch (const std::exception& e) {
                creds_parse_error = true;
                DP_LOG_ERROR("main", std::string("credentials parse failed: ") + e.what());
            }
        }

        // ---- widget UI files --------------------------------------------------
        std::vector<fs::path> asset_candidates = {fs::path(config_dir).parent_path() /
                                                      "assets" / "widget",
                                                  fs::path("/opt/droppoint/assets/widget")};
        std::string icon_dir;
        for (const auto& a : asset_candidates) {
            if (DirExists(a)) {
                icon_dir = a.string();
                break;
            }
        }
        const std::string widget_dir = (fs::path(log_dir).parent_path() / "widget").string();
        bool widget_dir_ok = false;
        if (!icon_dir.empty()) {
            std::vector<std::string> payload_names;
            for (const auto& p : cfg.payloads) payload_names.push_back(p.name);
            try {
                droppoint::hw::GenerateWidgetDir(widget_dir, icon_dir, payload_names);
                widget_dir_ok = true;
                DP_LOG_INFO("main", "widget files generated at " + widget_dir +
                                        " (icons from " + icon_dir + ")");
            } catch (const std::exception& e) {
                DP_LOG_ERROR("main", std::string("widget dir generation failed: ") + e.what());
            }
        } else {
            DP_LOG_ERROR("main", "no widget icon assets found (searched " +
                                     (fs::path(config_dir).parent_path() / "assets" / "widget")
                                         .string() +
                                     ", /opt/droppoint/assets/widget)");
        }

        // ---- terrain ----------------------------------------------------------
        // dem_directory from system.yaml; fall back to tiles shipped inside
        // the config dir (DPK packages carry config/dem/) or the app dir.
        std::string dem_dir = cfg.system.dem_directory;
        if (!DirExists(dem_dir)) {
            for (const fs::path& alt : {fs::path(config_dir) / "dem",
                                        fs::path(config_dir).parent_path() / "data" / "dem"}) {
                if (DirExists(alt)) {
                    dem_dir = alt.string();
                    break;
                }
            }
            DP_LOG_INFO("main", "DEM directory: " + dem_dir);
        }
        droppoint::hw::TerrainProvider terrain({dem_dir,
                                                cfg.system.allow_raw_dem,
                                                cfg.system.allow_flat_terrain_fallback});

        // ---- release backend (created early so the safe-exit hooks cover it) --
        std::unique_ptr<droppoint::hw::PwmServoRelease> pwm_release;
        std::unique_ptr<droppoint::psdk::PsdkWidgetRelease> widget_release;
        std::unique_ptr<droppoint::app::ExternalOperatorRelease> ext_release;
        droppoint::hal::IRelease* release = nullptr;
        switch (cfg.aircraft.release.backend) {
            case droppoint::util::config::ReleaseBackend::kPwmServo:
                pwm_release = std::make_unique<droppoint::hw::PwmServoRelease>(
                    droppoint::hw::PwmServoConfig{cfg.aircraft.release.pwm_chip_path,
                                                  cfg.aircraft.release.pwm_channel,
                                                  cfg.aircraft.release.period_ns,
                                                  cfg.aircraft.release.safe_pulse_ns,
                                                  cfg.aircraft.release.release_pulse_ns});
                release = pwm_release.get();
                break;
            case droppoint::util::config::ReleaseBackend::kPsdkWidget:
                widget_release = std::make_unique<droppoint::psdk::PsdkWidgetRelease>(
                    cfg.aircraft.release);
                release = widget_release.get();
                break;
            case droppoint::util::config::ReleaseBackend::kExternalOperator:
            default:
                ext_release = std::make_unique<droppoint::app::ExternalOperatorRelease>();
                release = ext_release.get();
                break;
        }
        g_release_for_shutdown = release;

        // The atexit handler reads g_release_for_shutdown, but the object lives
        // in the unique_ptrs above — on ANY return from this scope they would
        // die first and the handler would fire on a dangling pointer. This
        // guard is declared last so it destructs FIRST: it drives the release
        // safe while it is still alive and then clears the global, so the
        // atexit handler becomes a no-op on every exit path (normal return,
        // early return, exception unwind).
        struct ReleaseShutdownGuard {
            ~ReleaseShutdownGuard() {
                SafeShutdown();
                g_release_for_shutdown = nullptr;
            }
        } release_guard;

        // ---- selftest --no-link: everything that needs no aircraft ----------
        if (selftest && no_link) {
            droppoint::hw::SelftestFacts si;
            si.config = &cfg;
            si.credentials_parsed = creds.has_value();
            si.credentials_placeholder =
                creds.has_value() && droppoint::psdk::IsExampleCredentials(*creds);
            si.credentials_path = credentials_path;
            si.credentials_parse_error = creds_parse_error;
            si.link = false;
            si.release = release;
            si.dem_tile_count = terrain.DemTileCount();
            si.widget_dir = widget_dir;
            si.widget_dir_ok = widget_dir_ok;
            bool had_fail = false;
            const std::string report = droppoint::hw::BuildSelftestReport(si, &had_fail);
            std::fputs(report.c_str(), stdout);
            std::fflush(stdout);
            std::error_code ec;
            std::filesystem::create_directories(log_dir, ec);
            FILE* f = std::fopen((fs::path(log_dir) / "selftest.txt").string().c_str(), "w");
            if (f != nullptr) {
                std::fputs(report.c_str(), f);
                std::fclose(f);
            }
            return had_fail ? 1 : 0;
        }

        // ---- everything below needs the aircraft link -----------------------
        if (!creds) {
            throw std::runtime_error(
                "no usable credentials (searched --credentials, <config>/credentials.yaml, "
                "/etc/droppoint/credentials.yaml)");
        }

        droppoint::psdk::RegisterPlatformHandlers(cfg.aircraft);

        const T_DjiUserInfo user_info = MakeUserInfo(*creds);
        CheckDji(DjiCore_Init(&user_info), "DjiCore_Init");

        T_DjiAircraftInfoBaseInfo base_info{};
        CheckDji(DjiAircraftInfo_GetBaseInfo(&base_info), "DjiAircraftInfo_GetBaseInfo");
        const char* reported =
            droppoint::hw::conv::ModelNameFromAircraftType(static_cast<int>(base_info.aircraftType));
        const std::string reported_model = reported ? reported : "unknown";
        const bool model_matches = (reported_model == cfg.aircraft.model);
        if (!model_matches) {
            throw std::runtime_error("aircraft model mismatch: FC reports '" + reported_model +
                                     "' but aircraft.yaml says '" + cfg.aircraft.model +
                                     "' — fix the profile, do not fly like this");
        }

        CheckDji(DjiCore_SetAlias("DropPoint"), "DjiCore_SetAlias");
        T_DjiFirmwareVersion fw{};
        fw.majorVersion = static_cast<uint8_t>(droppoint::util::kVersionMajor);
        fw.minorVersion = static_cast<uint8_t>(droppoint::util::kVersionMinor);
        fw.modifyVersion = static_cast<uint8_t>(droppoint::util::kVersionPatch);
        fw.debugVersion = 0;
        CheckDji(DjiCore_SetFirmwareVersion(fw), "DjiCore_SetFirmwareVersion");

        // ---- probe-widgets: needs the link, no guidance loop -----------------
        if (probe_position >= 0) {
            CheckDji(DjiCore_ApplicationStart(), "DjiCore_ApplicationStart");
            const std::string probe_dir = (fs::path(log_dir) / "probe").string();
            return droppoint::psdk::ProbeWidgets(probe_position, probe_dir);
        }

        // ---- operator widgets -------------------------------------------------
        droppoint::hw::OperatorInputs operator_inputs(
            static_cast<int>(cfg.payloads.size()));
        if (widget_dir_ok) {
            droppoint::psdk::RegisterOperatorWidgets(operator_inputs, widget_dir);
        } else {
            DP_LOG_ERROR("main", "widgets NOT registered — operator UI will be empty");
        }

        if (widget_release) {
            widget_release->Init();
        }

        // ---- telemetry ---------------------------------------------------------
        droppoint::psdk::PsdkTelemetry telemetry;
        telemetry.Init();

        CheckDji(DjiCore_ApplicationStart(), "DjiCore_ApplicationStart");
        DP_LOG_INFO("main", "PSDK application started");

        // ---- HAL adapters -------------------------------------------------------
        droppoint::psdk::PsdkGimbal gimbal(telemetry, cfg.aircraft.gimbal_yaw_source);
        droppoint::psdk::FloatingWindowDisplay display;

        droppoint::psdk::NullRangefinder null_rangefinder;
        std::unique_ptr<droppoint::psdk::PsdkRangefinder> psdk_rangefinder;
        droppoint::hal::IRangefinder* rangefinder = &null_rangefinder;
        if (cfg.aircraft.advanced_features && cfg.aircraft.rangefinder_present) {
            psdk_rangefinder = std::make_unique<droppoint::psdk::PsdkRangefinder>(
                cfg.aircraft.camera_mount_position);
            try {
                psdk_rangefinder->Init();
                rangefinder = psdk_rangefinder.get();
            } catch (const std::exception& e) {
                DP_LOG_WARN("main", std::string("rangefinder disabled: ") + e.what());
                psdk_rangefinder.reset();
            }
        }

        droppoint::hw::LoggingAudio logging_audio;
        std::unique_ptr<droppoint::psdk::HmsAudio> hms_audio;
        droppoint::hal::IAudio* audio = &logging_audio;
        if (cfg.aircraft.psdk_platform == droppoint::util::config::PsdkPlatform::kManifold3 &&
            cfg.aircraft.advanced_features) {
            hms_audio = std::make_unique<droppoint::psdk::HmsAudio>();
            try {
                hms_audio->Init();
                audio = hms_audio.get();
            } catch (const std::exception& e) {
                DP_LOG_WARN("main", std::string("HMS audio disabled: ") + e.what());
                hms_audio.reset();
            }
        }

        droppoint::hw::NullLiveview liveview;

        // ---- selftest WITH link -------------------------------------------------
        if (selftest) {
            droppoint::hw::SelftestFacts si;
            si.config = &cfg;
            si.credentials_parsed = true;
            si.credentials_placeholder = droppoint::psdk::IsExampleCredentials(*creds);
            si.credentials_path = credentials_path;
            si.link = true;
            si.model_matches = model_matches;
            si.aircraft_reported = reported_model;
            si.telemetry_present = true;
            for (const auto& t : telemetry.TopicStatus()) {
                si.topics.push_back({t.name, t.subscribed, t.frequency_hz, t.required});
            }
            si.gnss = telemetry.GetGnssStatus();
            si.rangefinder_initialised =
                psdk_rangefinder != nullptr && psdk_rangefinder->IsInitialised();
            si.release = release;
            si.dem_tile_count = terrain.DemTileCount();
            si.home_tie = terrain.HasHomeTie();
            si.home_tie_alt_m = terrain.HomeTieAltitudeM();
            si.widget_dir = widget_dir;
            si.widget_dir_ok = widget_dir_ok;
            bool had_fail = false;
            const std::string report = droppoint::hw::BuildSelftestReport(si, &had_fail);
            std::fputs(report.c_str(), stdout);
            std::fflush(stdout);
            FILE* f = std::fopen((fs::path(log_dir) / "selftest.txt").string().c_str(), "w");
            if (f != nullptr) {
                std::fputs(report.c_str(), f);
                std::fclose(f);
            }
            return had_fail ? 1 : 0;
        }

        // ---- guidance loop config ------------------------------------------------
        droppoint::util::DropLog drop_log((fs::path(log_dir) / "drops.csv").string());

        droppoint::app::GuidanceLoopConfig loop_cfg;
        loop_cfg.gate_thresholds = cfg.system.gate_thresholds;
        if (cfg.payloads.empty()) {
            throw std::runtime_error("payloads.yaml defines no payload profiles");
        }
        loop_cfg.payload = cfg.payloads.front();
        loop_cfg.payloads = cfg.payloads;
        loop_cfg.dem_fn = [&terrain](double lat, double lon) {
            return terrain.Elevation(lat, lon);
        };
        loop_cfg.wind_alpha = cfg.system.wind_alpha;
        loop_cfg.rtk_required = cfg.aircraft.rtk_required;
        loop_cfg.ambient_temperature_c = cfg.system.ambient_temperature_c;
        loop_cfg.min_run_in_speed_mps = cfg.system.min_run_in_speed_mps;
        loop_cfg.release_hold_s = cfg.aircraft.release.hold_s;
        loop_cfg.wind.total_mass_kg =
            cfg.aircraft.mass_kg + cfg.aircraft.onboard_equipment_kg + loop_cfg.payload.mass_kg;
        loop_cfg.wind.cda_airframe = cfg.aircraft.cda_airframe;
        loop_cfg.wind.window_s = cfg.system.wind_window_s;
        loop_cfg.wind.min_hover_s = cfg.system.wind_min_hover_s;
        loop_cfg.wind.max_age_s = cfg.system.wind_max_age_s;
        loop_cfg.wind_model_calibrated = cfg.aircraft.airframe_calibrated;
        loop_cfg.drop_log = &drop_log;
        loop_cfg.monte_carlo_samples = cfg.system.monte_carlo_samples;
        loop_cfg.monte_carlo_rate_hz = cfg.system.monte_carlo_min_rate_hz;
        loop_cfg.floating_window_hz = cfg.system.floating_window_hz;
        loop_cfg.aircraft_mass_kg = cfg.aircraft.mass_kg;
        loop_cfg.onboard_equipment_kg = cfg.aircraft.onboard_equipment_kg;

        // Target source: gimbal crosshair raycast. Uses the first lens
        // profile that matches this airframe's camera_index = 0 stream.
        droppoint::core::projection::Intrinsics intrinsics{};
        if (!cfg.lenses.empty()) {
            intrinsics = cfg.lenses.front().intrinsics;
        }
        // ENU origin latches on the first >= 6-satellite fix inside Poll();
        // until then the loop just runs with a default origin and marks
        // aircraft state invalid (a no-fix aircraft cannot designate anyway).
        loop_cfg.origin = droppoint::core::geodesy::MakeEnuOrigin(droppoint::core::LatLonAlt{});
        droppoint::app::CrosshairTargetSource crosshair(
            telemetry, gimbal, intrinsics, loop_cfg.origin, loop_cfg.dem_fn,
            [&terrain](double lat, double lon) { return terrain.SourceFor(lat, lon); });

        droppoint::app::GuidanceLoop loop(telemetry, gimbal, *rangefinder, display, *audio,
                                          liveview, *release, {&crosshair}, loop_cfg);

        droppoint::hw::SdNotifyReady();
        DP_LOG_INFO("main", "guidance loop running at " +
                                std::to_string(cfg.system.guidance_loop_hz) + " Hz");

        const auto tick_period = std::chrono::microseconds(
            static_cast<long long>(1e6 / cfg.system.guidance_loop_hz));
        bool home_tie_frozen = false;
        bool logged_airborne_no_tie = false;

        while (g_running) {
            const auto tick_start = std::chrono::steady_clock::now();
            telemetry.Poll();

            // Home tie: keep refreshing the recorded ground altitude while
            // the aircraft is on the ground; freeze at takeoff.
            const auto flight_status = telemetry.LatestFlightStatus();
            if (!home_tie_frozen && telemetry.HasEnuOrigin()) {
                const auto lla = telemetry.EnuOriginLla();
                const auto state = telemetry.GetAircraftState();
                if (flight_status.has_value() && *flight_status == 2) {
                    home_tie_frozen = true;
                    DP_LOG_INFO("main", "airborne — home tie frozen");
                } else if (state.valid) {
                    terrain.SetHomeTie(state.geodetic.lat_deg, state.geodetic.lon_deg,
                                       state.geodetic.alt_m_hae);
                }
            }
            if (flight_status.has_value() && *flight_status == 2 && !terrain.HasHomeTie() &&
                !logged_airborne_no_tie) {
                logged_airborne_no_tie = true;
                DP_LOG_WARN("main", "started already airborne — no home tie; terrain falls "
                                    "back per system.yaml rules");
            }

            // Feed the live status the widgets report back to Pilot 2.
            droppoint::hw::OperatorInputs::Status st;
            const auto mid = loop.Mission().Id();
            st.armed_or_countdown = (mid == droppoint::app::MissionStateId::kArmed ||
                                     mid == droppoint::app::MissionStateId::kCountdown);
            st.mode = loop.Mission().Mode();
            st.active_payload_index = loop.ActivePayloadIndex();
            st.ambient_temperature_c = loop.AmbientTemperatureC();
            st.surface_index = loop.SurfaceIndex();
            operator_inputs.UpdateStatus(st);

            auto inputs = operator_inputs.Drain(NowUs());
            const auto solution = loop.Tick(inputs);
            (void)solution;

            droppoint::hw::SdNotifyWatchdog();

            const auto elapsed = std::chrono::steady_clock::now() - tick_start;
            if (elapsed < tick_period) {
                std::this_thread::sleep_for(tick_period - elapsed);
            }
        }

        DP_LOG_INFO("main", "shutting down");
        release->SetSafe();
        droppoint::hw::SdNotifyStopping();
        return 0;
    } catch (const std::exception& e) {
        DP_LOG_ERROR("main", std::string("fatal: ") + e.what());
        SafeShutdown();
        return 1;
    }
}
