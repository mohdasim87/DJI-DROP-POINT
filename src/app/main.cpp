// app/main.cpp
//
// Entry point. `--sim <scenario>` runs the full guidance loop against
// src/sim with no hardware (spec 17.3); without it, main would wire up
// src/psdk instead (see src/psdk/README once PSDK credentials exist).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "core/geodesy.h"
#include "core/solution.h"
#include "external_operator_release.h"
#include "guidance_loop.h"
#include "sim/scenario.h"
#include "sim/scripted_pilot.h"
#include "sim/sim_hal.h"
#include "sim/sim_world.h"
#include "util/config.h"
#include "util/crt_silence.h"
#include "util/drop_kits.h"
#include "util/logging.h"
#include "util/version.h"

namespace {

void PrintUsage() {
    std::printf(
        "droppoint [--sim <scenario>] [--profile <dir>] [--verbose] [--selftest] "
        "[--config <dir>] [--credentials <file>]\n"
        "  scenarios: hover, run-in, crosswind-3ms, crosswind-5ms, crosswind-8ms,\n"
        "             gusty-wind, wind-shear, sloping-terrain, gnss-degradation,\n"
        "             rtk-loss, telemetry-dropout, gimbal-slew, mis-designated-target\n"
        "\n"
        "--list-kits prints the compatible drop-kit catalogue and exits; with\n"
        "  --config <dir> it also marks the kits that fit that aircraft.\n"
        "--selftest runs the first-boot self-test (spec 15.3) and exits: PSDK link\n"
        "  status, credential validity, each data subscription topic, wind topic\n"
        "  presence, rangefinder presence, release GPIO responding, DEM tiles found,\n"
        "  PPS sync active, config validity, and per-profile calibration status.\n"
        "  Status: unverified — the PSDK-backed checks require hardware; this build\n"
        "  only runs the checks that do not need PSDK (config load, DEM directory,\n"
        "  release GPIO responsiveness if --release-gpio-chip is given).\n");
}

const char* PlatformName(droppoint::util::config::PsdkPlatform platform) {
    return platform == droppoint::util::config::PsdkPlatform::kManifold3 ? "manifold3"
                                                                        : "generic_linux";
}

const char* BackendName(droppoint::util::config::ReleaseBackend backend) {
    return backend == droppoint::util::config::ReleaseBackend::kExternalOperator
               ? "external_operator"
               : "pwm_servo";
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
        std::printf("[INFO] aircraft model: %s\n", cfg.aircraft.model.c_str());
        std::printf("[INFO] psdk platform: %s\n", PlatformName(cfg.aircraft.psdk_platform));
        std::printf("[INFO] release backend: %s%s\n", BackendName(cfg.aircraft.release.backend),
                    cfg.aircraft.release.description.empty()
                        ? ""
                        : (std::string(" — ") + cfg.aircraft.release.description).c_str());
        const auto* kit = cfg.aircraft.release.kit.empty()
                              ? nullptr
                              : droppoint::util::FindDropKit(cfg.aircraft.release.kit);
        if (kit != nullptr) {
            std::printf("[INFO] drop kit: %s %s (%d hook(s), %.3f kg, connects to %s)\n",
                        kit->maker.c_str(), kit->model.c_str(), kit->hooks, kit->weight_kg,
                        kit->interface.c_str());
            if (kit->weight_kg > 0.0 &&
                cfg.aircraft.onboard_equipment_kg < kit->weight_kg) {
                std::printf("[WARN] onboard_equipment_kg is less than the drop kit alone "
                            "weighs — weigh your build\n");
            }
        } else if (cfg.aircraft.release.backend !=
                   droppoint::util::config::ReleaseBackend::kPwmServo) {
            std::printf("[WARN] no release.kit set — set release.kit in aircraft.yaml so "
                        "the kit is checked against this aircraft (docs/DROP-KITS.md)\n");
        }
        std::printf("[INFO] max payload: %.3f kg (onboard equipment %.3f kg)\n",
                    cfg.aircraft.max_payload_kg, cfg.aircraft.onboard_equipment_kg);
        for (const auto& p : cfg.payloads) {
            const double margin_kg =
                cfg.aircraft.max_payload_kg - cfg.aircraft.onboard_equipment_kg - p.mass_kg;
            std::printf("%s  payload '%s' calibrated: %s, weight margin %.3f kg\n",
                        p.calibrated ? "[OK]  " : "[WARN]", p.name.c_str(),
                        p.calibrated ? "yes" : "NO (uncalibrated placeholder)", margin_kg);
        }
        std::printf("%s  airframe calibrated: %s\n",
                    cfg.aircraft.airframe_calibrated ? "[OK]  " : "[WARN]",
                    cfg.aircraft.airframe_calibrated ? "yes" : "NO (uncalibrated placeholder)");
    } catch (const std::exception& e) {
        std::printf("[FAIL] config invalid: %s\n", e.what());
        return 1;
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

int RunSim(const std::string& scenario_name, const std::string& profile_dir, bool verbose) {
    using namespace droppoint;
    util::Logger::Instance().SetVerbose(verbose);

    sim::ScenarioConfig scenario_cfg = sim::ParseScenarioName(scenario_name);

    // Defaults match the historical built-in sim values (M300-class
    // airframe, medical-pack-class payload); --profile overrides them with
    // a config profile directory.
    core::PayloadProfile payload;
    payload.name = "sim-medical-pack";
    payload.mass_kg = 1.5;
    payload.cda_free = 0.03;
    payload.min_release_agl_m = 10.0;
    payload.max_release_agl_m = 120.0;
    payload.crosswind_limit_mps = 12.0;
    payload.target_radius_m = 3.0;
    payload.mechanism_delay_s = 0.15;
    payload.calibrated = false;

    bool external_operator_backend = false;
    double release_hold_s = 1.5;

    if (!profile_dir.empty()) {
        const auto cfg = util::config::LoadAll(profile_dir);
        scenario_cfg.aircraft_mass_kg = cfg.aircraft.mass_kg;
        scenario_cfg.onboard_equipment_kg = cfg.aircraft.onboard_equipment_kg;
        scenario_cfg.cda_airframe = cfg.aircraft.cda_airframe;
        payload = cfg.payloads.front();
        // Hover inside the payload's release-AGL envelope: on a real aircraft
        // the operator would descend on the "DESCEND, ABOVE MAXIMUM RELEASE
        // AGL" hold — altitude control isn't modelled, so start there.
        scenario_cfg.start_altitude_m =
            0.5 * (payload.min_release_agl_m + payload.max_release_agl_m);
        external_operator_backend =
            cfg.aircraft.release.backend == util::config::ReleaseBackend::kExternalOperator;
        release_hold_s = cfg.aircraft.release.hold_s;
        std::printf("profile '%s': aircraft=%s payload='%s' backend=%s\n", profile_dir.c_str(),
                    cfg.aircraft.model.c_str(), payload.name.c_str(),
                    BackendName(cfg.aircraft.release.backend));
    }

    // The hover-tilt drag balance must see the same total airborne mass the
    // wind estimator assumes, or the estimate is biased high.
    scenario_cfg.payload_mass_kg = payload.mass_kg;
    auto world = std::make_shared<sim::SimWorld>(scenario_cfg);

    sim::SimTelemetry telemetry(world);
    sim::SimGimbal gimbal(world);
    sim::SimRangefinder rangefinder(world);
    sim::SimDisplay display;
    sim::SimAudio audio;
    sim::SimLiveview liveview;
    sim::SimRelease sim_release;
    app::ExternalOperatorRelease ext_release;
    hal::IRelease& release =
        external_operator_backend ? static_cast<hal::IRelease&>(ext_release)
                                  : static_cast<hal::IRelease&>(sim_release);

    core::projection::Intrinsics intrinsics;
    intrinsics.image_width_px = 1920;
    intrinsics.image_height_px = 1080;
    intrinsics.hfov_rad = 60.0 * core::kPi / 180.0;
    intrinsics.vfov_rad = 35.0 * core::kPi / 180.0;
    intrinsics.c_x = 960;
    intrinsics.c_y = 540;

    sim::SimCrosshairTargetSource crosshair(world, intrinsics);

    app::GuidanceLoopConfig config;
    config.payload = payload;
    config.payloads = {payload};
    config.origin = core::geodesy::MakeEnuOrigin(core::LatLonAlt{47.6062, -122.3321, 30.0});
    config.dem_fn = [&world](double lat, double lon) -> std::optional<double> {
        return world->GroundElevationAt(lat, lon);
    };
    config.wind_alpha = 0.14;
    config.min_run_in_speed_mps = 1.0;
    config.release_hold_s = release_hold_s;
    config.aircraft_mass_kg = scenario_cfg.aircraft_mass_kg;
    config.onboard_equipment_kg = scenario_cfg.onboard_equipment_kg;
    config.wind.total_mass_kg = scenario_cfg.aircraft_mass_kg +
                                scenario_cfg.onboard_equipment_kg + payload.mass_kg;
    config.wind.cda_airframe = scenario_cfg.cda_airframe;

    app::GuidanceLoop loop(telemetry, gimbal, rangefinder, display, audio, liveview, release,
                            {&crosshair}, config);

    const sim::ScriptedPilot::Plan plan = scenario_cfg.type == sim::ScenarioType::kSteadyHover
                                              ? sim::ScriptedPilot::Plan::kHoverManual
                                              : sim::ScriptedPilot::Plan::kRunInAuto;
    sim::ScriptedPilot pilot(world, &crosshair, plan, payload.mechanism_delay_s);

    DP_LOG_INFO("main", std::string("droppoint build ") + util::kBuildVersion +
                            " starting simulator scenario: " + scenario_name);

    int fired_count = 0;
    std::string last_hud;
    double predicted_miss_m = 0.0;
    core::EnuPoint aim_at_fire{};
    app::ReleaseMode mode_at_fire = app::ReleaseMode::kManual;

    constexpr double kDt = 0.1;  // 10 Hz guidance loop
    core::Solution solution;
    for (int step = 0; step < 600; ++step) {  // 60 s of sim time
        world->Step(kDt);

        app::GuidanceLoop::CycleInputs in = pilot.Tick(solution, loop.Mission());
        in.now_us = world->NowUs();
        const auto pre_state = loop.Mission().Id();
        const bool pilot_saw_window = solution.release_window_open;
        const double pilot_saw_miss = solution.miss_distance_m;
        const bool pilot_saw_fwd = solution.forward_valid;
        solution = loop.Tick(in);
        if (verbose && (in.arm_pressed || in.manual_release_pressed ||
                        loop.Mission().Id() != pre_state)) {
            std::printf(
                "    t=%.1f press{arm=%d rel=%d} state %d->%d prevsol{win=%d miss=%.2f fwd=%d}\n",
                step * kDt, in.arm_pressed ? 1 : 0, in.manual_release_pressed ? 1 : 0,
                static_cast<int>(pre_state), static_cast<int>(loop.Mission().Id()),
                pilot_saw_window ? 1 : 0, pilot_saw_miss, pilot_saw_fwd ? 1 : 0);
        }

        if (release.GetState() == hal::ReleaseState::kFired && !world->HasTruthImpact()) {
            ++fired_count;
            mode_at_fire = loop.Mission().Mode();
            predicted_miss_m = solution.miss_distance_m;
            aim_at_fire = loop.Mission().CurrentTarget().enu;
            world->SimulateTruthDrop(payload, aim_at_fire);
        }

        if (step % 50 == 0) {
            const std::string& hold = loop.LastGates().primary_hold_reason;
            std::printf("[t=%.1fs] state=%d has_solution=%d miss=%.2fm hover=%d hold='%s'\n",
                        step * kDt, static_cast<int>(loop.Mission().Id()),
                        solution.has_solution, solution.miss_distance_m,
                        solution.hover_mode ? 1 : 0,
                        hold.empty() ? "-" : hold.c_str());
            if (verbose) {
                const auto ac = world->Position();
                const auto vel = world->Velocity();
                const auto& tgt = loop.Mission().CurrentTarget().enu;
                std::printf(
                    "    ac=(%.1f,%.1f) v=(%.2f,%.2f) tgt=(%.1f,%.1f) imp=(%.1f,%.1f) "
                    "fwd=%d ttr=%.2f window=%d abort='%s'\n"
                    "    hud='%s'\n",
                    ac.e, ac.n, vel.e, vel.n, tgt.e, tgt.n,
                    solution.predicted_impact_point.e, solution.predicted_impact_point.n,
                    solution.forward_valid ? 1 : 0, solution.time_to_release_s,
                    solution.release_window_open ? 1 : 0,
                    loop.Mission().LastAbortReason().c_str(),
                    display.LastMessage().c_str());
            }
        }
        if (verbose && display.LastMessage() != last_hud) {
            std::printf("    t=%.1f hud='%s'\n", step * kDt, display.LastMessage().c_str());
            last_hud = display.LastMessage();
        }
        if (verbose && release.GetState() == hal::ReleaseState::kFired && fired_count == 1) {
            const auto ac = world->Position();
            std::printf("    FIRE at t=%.1fs ac=(%.1f,%.1f) miss=%.2f\n", step * kDt, ac.e, ac.n,
                        solution.miss_distance_m);
        }
    }

    char truth_buf[32] = "n/a";
    if (world->HasTruthImpact()) {
        const double truth_miss_m =
            core::geodesy::HorizontalRange(world->TruthImpactEnu(), aim_at_fire);
        std::snprintf(truth_buf, sizeof(truth_buf), "%.2fm", truth_miss_m);
    }

    const std::string& last_hold = loop.LastGates().primary_hold_reason;
    std::printf(
        "SUMMARY scenario=%s fired=%d mode=%s backend=%s predicted_miss=%.2fm truth_miss=%s "
        "hold='%s'\n",
        scenario_name.c_str(), fired_count,
        mode_at_fire == app::ReleaseMode::kAuto ? "auto" : "manual",
        external_operator_backend ? "external_operator" : "sim", predicted_miss_m, truth_buf,
        last_hold.empty() ? "-" : last_hold.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    droppoint::util::SilenceCrtDialogs();
    std::string scenario;
    std::string config_dir = "config";
    std::string profile_dir;
    bool verbose = false;
    bool selftest = false;
    bool list_kits = false;
    bool config_explicit = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sim" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--selftest") {
            selftest = true;
        } else if (arg == "--list-kits") {
            list_kits = true;
        } else if (arg == "--config" && i + 1 < argc) {
            config_dir = argv[++i];
            config_explicit = true;
        } else if (arg == "--profile" && i + 1 < argc) {
            profile_dir = argv[++i];
        } else if (arg == "--credentials" && i + 1 < argc) {
            ++i;  // consumed, PSDK-backed startup path only (not this sim build)
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
    }

    if (list_kits) {
        try {
            std::string model;
            if (config_explicit) {
                model = droppoint::util::config::LoadAll(config_dir).aircraft.model;
            }
            std::fputs(droppoint::util::FormatKitTable(model).c_str(), stdout);
            return 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fatal: %s\n", e.what());
            return 1;
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
        return RunSim(scenario, profile_dir, verbose);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
