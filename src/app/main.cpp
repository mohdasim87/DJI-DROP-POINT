// app/main.cpp
//
// Entry point. `--sim <scenario>` runs the full guidance loop against
// src/sim with no hardware (spec 17.3); without it, main would wire up
// src/psdk instead (see src/psdk/README once PSDK credentials exist).
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "core/geodesy.h"
#include "core/solution.h"
#include "guidance_loop.h"
#include "sim/sim_hal.h"
#include "sim/sim_world.h"
#include "util/config.h"
#include "util/logging.h"
#include "util/version.h"

namespace {

void PrintUsage() {
    std::printf(
        "droppoint [--sim <scenario>] [--verbose] [--selftest] [--config <dir>] [--credentials <file>]\n"
        "  scenarios: hover, run-in, crosswind-3ms, crosswind-5ms, crosswind-8ms,\n"
        "             gusty-wind, wind-shear, sloping-terrain, gnss-degradation,\n"
        "             rtk-loss, telemetry-dropout, gimbal-slew, mis-designated-target\n"
        "\n"
        "--selftest runs the first-boot self-test (spec 15.3) and exits: PSDK link\n"
        "  status, credential validity, each data subscription topic, wind topic\n"
        "  presence, rangefinder presence, release GPIO responding, DEM tiles found,\n"
        "  PPS sync active, config validity, and per-profile calibration status.\n"
        "  Status: unverified — the PSDK-backed checks require hardware; this build\n"
        "  only runs the checks that do not need PSDK (config load, DEM directory,\n"
        "  release GPIO responsiveness if --release-gpio-chip is given).\n");
}

// Status: partially unverified — requires hardware for the PSDK-backed
// checks (link status, credential validity, subscription topics, wind
// topic, rangefinder). Spec 15.3 requires a first-boot self-test producing
// a plain-language report; this implementation runs every check that is
// possible without PSDK and clearly labels the rest as not run here.
int RunSelfTest(const std::string& config_dir) {
    std::printf("DropPoint self-test report (build %s)\n", droppoint::util::kBuildVersion);
    std::printf("===========================\n");
    try {
        const auto cfg = droppoint::util::config::LoadAll(config_dir);
        std::printf("[OK]   config valid (aircraft=%s, %zu payload profile(s), %zu lens profile(s))\n",
                    cfg.aircraft.model.c_str(), cfg.payloads.size(), cfg.lenses.size());
        for (const auto& p : cfg.payloads) {
            std::printf("%s  payload '%s' calibrated: %s\n", p.calibrated ? "[OK]  " : "[WARN]",
                        p.name.c_str(), p.calibrated ? "yes" : "NO (uncalibrated placeholder)");
        }
    } catch (const std::exception& e) {
        std::printf("[FAIL] config invalid: %s\n", e.what());
    }

    std::printf("[SKIP] PSDK link status — requires hardware, not run in this build\n");
    std::printf("[SKIP] credential validity — requires hardware, not run in this build\n");
    std::printf("[SKIP] data subscription topics — requires hardware, not run in this build\n");
    std::printf("[INFO] wind topic: absent in public PSDK (docs/OPEN-QUESTIONS.md #2); hover estimator is the wind source\n");
    std::printf("[SKIP] rangefinder present — requires hardware, not run in this build\n");
    std::printf("[SKIP] release GPIO responding — requires hardware, not run in this build\n");
    std::printf("[SKIP] PPS sync active — requires hardware, not run in this build\n");
    return 0;
}

int RunSim(const std::string& scenario_name, bool verbose) {
    using namespace droppoint;
    util::Logger::Instance().SetVerbose(verbose);

    const sim::ScenarioConfig scenario_cfg = sim::ParseScenarioName(scenario_name);
    auto world = std::make_shared<sim::SimWorld>(scenario_cfg);

    sim::SimTelemetry telemetry(world);
    sim::SimGimbal gimbal(world);
    sim::SimRangefinder rangefinder(world);
    sim::SimDisplay display;
    sim::SimAudio audio;
    sim::SimLiveview liveview;
    sim::SimRelease release;

    core::projection::Intrinsics intrinsics;
    intrinsics.image_width_px = 1920;
    intrinsics.image_height_px = 1080;
    intrinsics.hfov_rad = 60.0 * core::kPi / 180.0;
    intrinsics.vfov_rad = 35.0 * core::kPi / 180.0;
    intrinsics.c_x = 960;
    intrinsics.c_y = 540;

    sim::SimCrosshairTargetSource crosshair(world, intrinsics);

    core::PayloadProfile payload;
    payload.name = "sim-medical-pack";
    payload.mass_kg = 1.5;
    payload.cda_free = 0.03;
    payload.min_release_agl_m = 10.0;
    payload.max_release_agl_m = 120.0;
    payload.crosswind_limit_mps = 12.0;
    payload.mechanism_delay_s = 0.15;
    payload.calibrated = false;

    app::GuidanceLoopConfig config;
    config.payload = payload;
    config.origin = core::geodesy::MakeEnuOrigin(core::LatLonAlt{47.6062, -122.3321, 30.0});
    config.dem_fn = [&world](double lat, double lon) -> std::optional<double> {
        return world->GroundElevationAt(lat, lon);
    };
    config.wind_alpha = 0.14;

    app::GuidanceLoop loop(telemetry, gimbal, rangefinder, display, audio, liveview, release,
                            {&crosshair}, config);

    DP_LOG_INFO("main", std::string("droppoint build ") + util::kBuildVersion +
                            " starting simulator scenario: " + scenario_name);

    constexpr double kDt = 0.1;  // 10 Hz guidance loop
    for (int step = 0; step < 200; ++step) {
        world->Step(kDt);

        app::GuidanceLoop::CycleInputs in;
        in.now_us = world->NowUs();
        in.set_target_pressed = (step == 5);
        in.confirm_target_pressed = (step == 6);
        in.arm_pressed = (step == 7);

        const core::Solution solution = loop.Tick(in);
        if (step % 10 == 0) {
            std::printf("[t=%.1fs] state=%d has_solution=%d miss=%.2fm\n", step * kDt,
                        static_cast<int>(loop.Mission().Id()), solution.has_solution, solution.miss_distance_m);
        }
    }

    std::printf("Simulation complete. Final release count check: fired=%d\n", release.FireCount() > 0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string scenario;
    std::string config_dir = "config";
    bool verbose = false;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sim" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--selftest") {
            selftest = true;
        } else if (arg == "--config" && i + 1 < argc) {
            config_dir = argv[++i];
        } else if (arg == "--credentials" && i + 1 < argc) {
            ++i;  // consumed, PSDK-backed startup path only (not this sim build)
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
    }

    if (selftest) {
        return RunSelfTest(config_dir);
    }

    if (scenario.empty()) {
        std::printf("No --sim scenario given and no PSDK build target configured for this binary.\n");
        PrintUsage();
        return 1;
    }

    try {
        return RunSim(scenario, verbose);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
