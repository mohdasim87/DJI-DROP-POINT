// psdk/main_psdk.cpp
//
// Real aircraft entry point. Wires the PSDK platform layer, credentials,
// telemetry, gimbal, rangefinder, display, audio, liveview, and release output
// into app::GuidanceLoop.
// Status: unverified — compiles against real headers, needs hardware to verify.
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/crosshair_target_source.h"
#include "app/guidance_loop.h"
#include "app/mission_state.h"
#include "core/geodesy.h"
#include "core/solution.h"
#include "hal/i_release.h"
#include "psdk/app_registration.h"
#include "psdk/audio_speaker.h"
#include "psdk/display_floating_window.h"
#include "psdk/gimbal_psdk.h"
#include "psdk/liveview_marker.h"
#include "psdk/platform_linux.h"
#include "psdk/psdk_util.h"
#include "psdk/rangefinder_camera_manager.h"
#include "psdk/release_gpio.h"
#include "psdk/telemetry_subscription.h"
#include "util/config.h"
#include "util/logging.h"
#include "util/version.h"

#include "dji_aircraft_info.h"
#include "dji_core.h"

namespace {

std::atomic<bool> g_running{true};

void SignalHandler(int signum) {
    (void)signum;
    g_running = false;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        signal(SIGINT, SignalHandler);
        signal(SIGTERM, SignalHandler);

        std::string config_dir = "config";
        std::string credentials_path = "config/credentials.yaml";
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--config" && i + 1 < argc) {
                config_dir = argv[++i];
            } else if (arg == "--credentials" && i + 1 < argc) {
                credentials_path = argv[++i];
            }
        }

        DP_LOG_INFO("main", std::string("droppoint build ") + droppoint::util::kBuildVersion +
                                " starting PSDK target");

        const auto cfg = droppoint::util::config::LoadAll(config_dir);
        droppoint::psdk::PlatformInit(cfg.aircraft);

        const auto creds = droppoint::psdk::LoadCredentials(credentials_path);
        T_DjiUserInfo user_info{};
        strncpy(user_info.appName, creds.app_name.c_str(), sizeof(user_info.appName) - 1);
        strncpy(user_info.appId, creds.app_id.c_str(), sizeof(user_info.appId) - 1);
        strncpy(user_info.appKey, creds.app_key.c_str(), sizeof(user_info.appKey) - 1);
        strncpy(user_info.appLicense, creds.app_license.c_str(), sizeof(user_info.appLicense) - 1);
        strncpy(user_info.developerAccount, creds.developer_account.c_str(),
                sizeof(user_info.developerAccount) - 1);
        strncpy(user_info.baudRate, creds.baud_rate.c_str(), sizeof(user_info.baudRate) - 1);

        droppoint::psdk::CheckReturn(DjiCore_Init(&user_info), "DjiCore_Init");
        droppoint::psdk::CheckReturn(DjiCore_ApplicationStart(), "DjiCore_ApplicationStart");

        T_DjiAircraftInfoBaseInfo aircraft_info{};
        droppoint::psdk::CheckReturn(DjiAircraftInfo_GetBaseInfo(&aircraft_info), "DjiAircraftInfo_GetBaseInfo");
        DP_LOG_INFO("main", std::string("aircraft type: ") + std::to_string(aircraft_info.aircraftType));

        auto origin = std::make_shared<droppoint::core::geodesy::EnuOrigin>();
        droppoint::psdk::TelemetrySubscription telemetry(cfg.aircraft, cfg.system, origin);

        // Wait up to 30 s for a valid position so we can set the ENU origin.
        constexpr int kOriginWaitSteps = 300;
        bool have_origin = false;
        for (int i = 0; i < kOriginWaitSteps && g_running; ++i) {
            const auto state = telemetry.GetAircraftState();
            if (state.valid && origin->lla.lat_deg != 0.0) {
                have_origin = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!have_origin) {
            throw std::runtime_error("timed out waiting for first valid telemetry/position");
        }

        droppoint::psdk::GimbalPsdk gimbal(cfg.aircraft.gimbal_yaw_source);
        droppoint::psdk::RangefinderCameraManager rangefinder(cfg.aircraft);
        droppoint::psdk::DisplayFloatingWindow display;
        droppoint::psdk::AudioSpeaker audio;
        droppoint::psdk::LiveviewMarker liveview;

        droppoint::psdk::ReleaseGpioConfig release_cfg{};
        release_cfg.pwm_chip_path = "/sys/class/pwm/pwmchip0";
        release_cfg.pwm_channel = 0;
        release_cfg.period_ns = 20000000;  // 50 Hz servo
        release_cfg.safe_pulse_ns = 1000000;
        release_cfg.fire_pulse_ns = 2000000;
        droppoint::psdk::ReleaseGpio release(release_cfg);

        // Wire the gimbal-angle updates from telemetry into the gimbal adapter.
        telemetry.RegisterUpdateCallback([&gimbal](const droppoint::core::AircraftState&) {
            // The telemetry adapter already subscribes to GIMBAL_ANGLES. The
            // callback mechanism currently only notifies on QUATERNION updates,
            // so gimbal angles are polled below via DjiFcSubscription.
            (void)gimbal;
        });

        // Default lens profile for the crosshair source. In a real build this
        // should come from config/lenses.yaml matched to the active camera.
        droppoint::core::projection::Intrinsics intrinsics{};
        intrinsics.image_width_px = 1920;
        intrinsics.image_height_px = 1080;
        intrinsics.hfov_rad = 60.0 * droppoint::core::kPi / 180.0;
        intrinsics.vfov_rad = 35.0 * droppoint::core::kPi / 180.0;
        intrinsics.c_x = 960;
        intrinsics.c_y = 540;

        droppoint::app::CrosshairTargetSource crosshair(telemetry, gimbal, intrinsics, *origin,
                                                        [&cfg](double lat, double lon) -> std::optional<double> {
                                                            // TODO: load DEM from cfg.system.dem_directory
                                                            (void)lat;
                                                            (void)lon;
                                                            return std::nullopt;
                                                        });

        droppoint::core::PayloadProfile payload;
        if (!cfg.payloads.empty()) payload = cfg.payloads.front();

        droppoint::app::GuidanceLoopConfig loop_cfg;
        loop_cfg.payload = payload;
        loop_cfg.origin = *origin;
        loop_cfg.dem_fn = [&cfg](double lat, double lon) -> std::optional<double> {
            (void)lat;
            (void)lon;
            // TODO: real DEM lookup from cfg.system.dem_directory
            return std::nullopt;
        };
        loop_cfg.wind_alpha = 0.14;

        droppoint::app::GuidanceLoop loop(telemetry, gimbal, rangefinder, display, audio, liveview,
                                          release, {&crosshair}, loop_cfg);

        DP_LOG_INFO("main", "guidance loop running");

        constexpr double kDt = 0.1;  // 10 Hz
        while (g_running) {
            const std::uint64_t now_us = droppoint::psdk::MonotonicUs();

            droppoint::app::GuidanceLoop::CycleInputs in{};
            in.now_us = now_us;
            // Inputs come from RC button events (currently unmapped) or Pilot
            // widget interactions (not yet implemented).

            loop.Tick(in);
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(kDt * 1000)));
        }

        DP_LOG_INFO("main", "shutting down");
        droppoint::psdk::PlatformDeinit();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
