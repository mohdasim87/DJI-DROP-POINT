// tests/test_guidance_loop_sim.cpp
//
// End-to-end guidance-loop tests run against the full simulator stack
// (SimWorld + sim HALs + ScriptedPilot + GuidanceLoop). These exercise the
// spec acceptance criteria that unit tests cannot: a hover drop in wind, an
// auto run-in in crosswind, the mis-designated-target safety case, a
// telemetry-dropout abort, and the operator-actuated release backend.
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/external_operator_release.h"
#include "app/guidance_loop.h"
#include "core/geodesy.h"
#include "core/math_constants.h"
#include "sim/scenario.h"
#include "sim/scripted_pilot.h"
#include "sim/sim_hal.h"
#include "sim/sim_world.h"
#include "test_framework.h"
#include "util/config.h"

using namespace droppoint;

namespace {

struct SimRunResult {
    int fired_count = 0;
    double predicted_miss_m = -1.0;
    double truth_miss_m = -1.0;
    bool saw_armed = false;
    bool aborted_from_armed = false;  // kArmed/kCountdown left without a release
    bool fired_during_dropout = false;
    bool nan_seen = false;
    bool display_too_long = false;
    std::string longest_display;
    bool drop_kit_text_seen = false;
    app::ReleaseMode final_mode = app::ReleaseMode::kManual;
    std::string abort_reason;
};

bool SolutionHasNan(const core::Solution& s) {
    if (!std::isfinite(s.miss_distance_m) || !std::isfinite(s.time_to_release_s) ||
        !std::isfinite(s.steer_distance_m) || !std::isfinite(s.steer_bearing_rad) ||
        !std::isfinite(s.miss_if_released_now_m)) {
        return true;
    }
    if (s.forward_valid && (!std::isfinite(s.impact_if_released_now.e) ||
                            !std::isfinite(s.impact_if_released_now.n) ||
                            !std::isfinite(s.impact_if_released_now.u))) {
        return true;
    }
    return false;
}

SimRunResult RunSimScenario(const sim::ScenarioConfig& scenario_cfg_in,
                            sim::ScriptedPilot::Plan plan, double seconds = 60.0,
                            bool external_backend = false, bool hold_fire = false,
                            const core::PayloadProfile* payload_override = nullptr) {
    sim::ScenarioConfig scenario_cfg = scenario_cfg_in;
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
    if (payload_override != nullptr) payload = *payload_override;
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
        external_backend ? static_cast<hal::IRelease&>(ext_release)
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
    config.aircraft_mass_kg = scenario_cfg.aircraft_mass_kg;
    config.onboard_equipment_kg = scenario_cfg.onboard_equipment_kg;
    config.wind.total_mass_kg = scenario_cfg.aircraft_mass_kg +
                                scenario_cfg.onboard_equipment_kg + payload.mass_kg;
    config.wind.cda_airframe = scenario_cfg.cda_airframe;

    app::GuidanceLoop loop(telemetry, gimbal, rangefinder, display, audio, liveview, release,
                           {&crosshair}, config);
    sim::ScriptedPilot pilot(world, &crosshair, plan, payload.mechanism_delay_s);
    pilot.SetHoldFire(hold_fire);

    SimRunResult result;
    bool was_armed = false;
    core::EnuPoint aim_at_fire{};

    constexpr double kDt = 0.1;  // 10 Hz guidance loop
    const int steps = static_cast<int>(seconds / kDt);
    core::Solution solution;
    for (int step = 0; step < steps; ++step) {
        world->Step(kDt);

        app::GuidanceLoop::CycleInputs in = pilot.Tick(solution, loop.Mission());
        in.now_us = world->NowUs();
        solution = loop.Tick(in);
        const double t = world->NowS();

        const app::MissionStateId id = loop.Mission().Id();
        if (id == app::MissionStateId::kArmed || id == app::MissionStateId::kCountdown) {
            result.saw_armed = true;
            was_armed = true;
        }
        if (was_armed && id == app::MissionStateId::kConfirmed) {
            result.aborted_from_armed = true;
            was_armed = false;
        }

        if (release.GetState() == hal::ReleaseState::kFired && !world->HasTruthImpact()) {
            ++result.fired_count;
            result.predicted_miss_m = solution.miss_distance_m;
            aim_at_fire = loop.Mission().CurrentTarget().enu;
            world->SimulateTruthDrop(payload, aim_at_fire);
            if (scenario_cfg.dropout_start_s >= 0.0 &&
                t >= scenario_cfg.dropout_start_s &&
                t <= scenario_cfg.dropout_start_s + scenario_cfg.dropout_duration_s + 0.5) {
                result.fired_during_dropout = true;
            }
        }

        if (SolutionHasNan(solution)) result.nan_seen = true;
        if (display.LastMessage().size() > 255) result.display_too_long = true;
        if (display.LastMessage().size() > result.longest_display.size()) {
            result.longest_display = display.LastMessage();
        }
        if (display.LastMessage().find("DROP KIT") != std::string::npos) {
            result.drop_kit_text_seen = true;
        }
    }

    if (world->HasTruthImpact()) {
        result.truth_miss_m =
            core::geodesy::HorizontalRange(world->TruthImpactEnu(), aim_at_fire);
    }
    result.final_mode = loop.Mission().Mode();
    result.abort_reason = loop.Mission().LastAbortReason();
    return result;
}

core::PayloadProfile SimPayload() {
    core::PayloadProfile p;
    p.target_radius_m = 3.0;
    return p;
}

}  // namespace

TEST_CASE("Sim: hover drop in a 5 m/s crosswind fires once inside the target radius") {
    auto cfg = sim::ParseScenarioName("crosswind-5ms");
    const auto r = RunSimScenario(cfg, sim::ScriptedPilot::Plan::kHoverManual);
    REQUIRE(!r.nan_seen);
    REQUIRE(!r.display_too_long);
    REQUIRE(r.fired_count == 1);
    REQUIRE(r.predicted_miss_m >= 0.0);
    REQUIRE(r.truth_miss_m >= 0.0);
    REQUIRE(r.truth_miss_m <= SimPayload().target_radius_m);
}

TEST_CASE("Sim: auto run-in in a 5 m/s crosswind fires once inside the target radius") {
    auto cfg = sim::ParseScenarioName("crosswind-5ms");
    const auto r = RunSimScenario(cfg, sim::ScriptedPilot::Plan::kRunInAuto);
    REQUIRE(!r.nan_seen);
    REQUIRE(!r.display_too_long);
    REQUIRE(r.fired_count == 1);
    REQUIRE(r.predicted_miss_m >= 0.0);
    REQUIRE(r.truth_miss_m >= 0.0);
    REQUIRE(r.truth_miss_m <= SimPayload().target_radius_m);
}

TEST_CASE("Sim: a mis-designated target never fires in auto mode") {
    auto cfg = sim::ParseScenarioName("mis-designated-target");
    const auto r = RunSimScenario(cfg, sim::ScriptedPilot::Plan::kRunInAuto);
    REQUIRE(!r.nan_seen);
    REQUIRE(r.fired_count == 0);
    // The run-in should have been attempted (armed) and then aborted by the
    // corridor check or a gate — never silently skipped.
    REQUIRE(r.saw_armed);
}

TEST_CASE("Sim: telemetry dropout aborts an armed auto run and never fires") {
    auto cfg = sim::ParseScenarioName("telemetry-dropout");
    const auto r = RunSimScenario(cfg, sim::ScriptedPilot::Plan::kRunInAuto);
    REQUIRE(!r.nan_seen);
    REQUIRE(r.saw_armed);
    REQUIRE(r.aborted_from_armed);
    REQUIRE(!r.abort_reason.empty());
    REQUIRE(r.fired_count == 0);
    REQUIRE(!r.fired_during_dropout);
}

TEST_CASE("Sim: operator-actuated backend rejects AUTO and says DROP KIT") {
    auto cfg = sim::ParseScenarioName("hover");
    const auto r = RunSimScenario(cfg, sim::ScriptedPilot::Plan::kHoverManual,
                                  /*seconds=*/60.0, /*external_backend=*/true,
                                  /*hold_fire=*/true);
    REQUIRE(!r.nan_seen);
    // AUTO was requested by the pilot every cycle; it must have stayed manual.
    REQUIRE(r.final_mode == app::ReleaseMode::kManual);
    // The pilot armed and held with the corridor open: the floating window
    // must have directed the operator to the kit's own release.
    REQUIRE(r.saw_armed);
    REQUIRE(r.drop_kit_text_seen);
    REQUIRE(r.fired_count == 0);
}

TEST_CASE("Sim: every scenario runs 60 s with no NaN and short floating text") {
    const std::vector<std::string> names = {
        "hover",      "run-in",            "crosswind-3ms",   "crosswind-5ms",
        "crosswind-8ms", "gusty-wind",     "wind-shear",      "sloping-terrain",
        "gnss-degradation", "rtk-loss",    "telemetry-dropout", "gimbal-slew",
        "mis-designated-target",
    };
    for (const auto& name : names) {
        auto cfg = sim::ParseScenarioName(name);
        const auto plan = cfg.type == sim::ScenarioType::kSteadyHover
                              ? sim::ScriptedPilot::Plan::kHoverManual
                              : sim::ScriptedPilot::Plan::kRunInAuto;
        const auto r = RunSimScenario(cfg, plan);
        REQUIRE(!r.nan_seen);
        REQUIRE(!r.display_too_long);
    }
}

TEST_CASE("Sim: hover drop fires once inside radius with each shipped profile") {
    // The packaged install path: --sim hover --profile <dir> must be able to
    // drop with the shipped M4T and M400 profiles. The profile supplies the
    // aircraft mass, onboard equipment, CdA, payload and (external-operator)
    // release backend; the scenario starts inside the payload's AGL envelope
    // just as the sim driver now does.
    for (const char* profile : {"profiles/m4t", "profiles/m400"}) {
        const auto cfg_all = util::config::LoadAll(DROPPOINT_CONFIG_DIR +
                                                 std::string("/") + profile);
        sim::ScenarioConfig scenario_cfg = sim::ParseScenarioName("hover");
        scenario_cfg.aircraft_mass_kg = cfg_all.aircraft.mass_kg;
        scenario_cfg.onboard_equipment_kg = cfg_all.aircraft.onboard_equipment_kg;
        scenario_cfg.cda_airframe = cfg_all.aircraft.cda_airframe;
        const core::PayloadProfile& payload = cfg_all.payloads.front();
        scenario_cfg.start_altitude_m =
            0.5 * (payload.min_release_agl_m + payload.max_release_agl_m);

        const auto r = RunSimScenario(
            scenario_cfg, sim::ScriptedPilot::Plan::kHoverManual, /*seconds=*/60.0,
            /*external_backend=*/
            cfg_all.aircraft.release.backend ==
                util::config::ReleaseBackend::kExternalOperator,
            /*hold_fire=*/false, &payload);
        REQUIRE(!r.nan_seen);
        REQUIRE(r.saw_armed);
        REQUIRE(r.fired_count == 1);
        REQUIRE(r.truth_miss_m >= 0.0);
        REQUIRE(r.truth_miss_m <= payload.target_radius_m);
    }
}
