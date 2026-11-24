// core/solution.h
//
// Plain data structures shared across the core/ algorithms. Nothing in this
// header depends on DJI, the OS, or any platform facility: structs in,
// structs out, per the project's core/ boundary rule.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace droppoint::core {

// A point in the local East-North-Up (ENU) tangent-plane frame, in metres,
// relative to a project-wide ENU origin (see geodesy.h).
struct EnuPoint {
    double e = 0.0;  // East, metres
    double n = 0.0;  // North, metres
    double u = 0.0;  // Up, metres
};

struct EnuVector {
    double e = 0.0;
    double n = 0.0;
    double u = 0.0;
};

// Geodetic coordinate, WGS-84.
struct LatLonAlt {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double alt_m_hae = 0.0;  // height above WGS-84 ellipsoid
};

// Aircraft kinematic state at one instant, already fused/converted into the
// local ENU frame. Populated by app/ from HAL telemetry; core/ never touches
// a HAL interface directly.
//
// Attitude conventions (fixed here, relied on by core::wind and app/):
//   yaw_rad   — ENU math angle: 0 = East, counter-clockwise positive.
//   pitch_rad — positive = nose up.
//   roll_rad  — positive = right wing down.
//   timestamp_us — local monotonic clock; the SAME domain as
//               CycleInputs.now_us in app/. Never mix with any other clock.
struct AircraftState {
    std::uint64_t timestamp_us = 0;  // monotonic, aligned to a single clock domain
    EnuPoint position;
    EnuVector velocity;
    double yaw_rad = 0.0;    // aircraft heading, ENU convention (0 = East, CCW positive)
    double pitch_rad = 0.0;  // positive = nose up
    double roll_rad = 0.0;   // positive = right wing down
    LatLonAlt geodetic;      // for DEM lookups and logging
    bool valid = false;
};

struct GimbalState {
    std::uint64_t timestamp_us = 0;
    double yaw_rad = 0.0;    // compass heading in the ground frame: 0 = North,
                           // clockwise positive (not the ENU math angle above)
    double pitch_rad = 0.0;  // 0 = level, negative = looking down (down = -pi/2)
    double roll_rad = 0.0;
    double zoom_ratio = 1.0;
    bool valid = false;
};

struct WindSample {
    double speed_mps = 0.0;
    double from_bearing_rad = 0.0;  // meteorological convention: direction wind comes FROM
    bool from_telemetry_topic = false;  // false => estimated from hover attitude fallback
    bool valid = false;
};

// Per-payload ballistic parameters, mirrors payloads.yaml.
struct PayloadProfile {
    std::string name;
    double mass_kg = 0.0;
    double cda_free = 0.0;         // m^2, free-fall drag area
    std::optional<double> cda_deployed;       // m^2, after streamer/chute deploys
    std::optional<double> deploy_delay_s;     // time after release before cda_deployed applies
    double min_release_agl_m = 0.0;
    double max_release_agl_m = 1e9;
    double crosswind_limit_mps = 1e9;
    double target_radius_m = 0.0;   // acceptable miss radius, used for CEP style reporting
    double mechanism_delay_s = 0.0; // measured servo/linkage delay, subtracted from lead time
    bool calibrated = false;
    std::string calibration_date;   // ISO-8601, empty if never calibrated
};

// Atmosphere at a point, see atmosphere.*
struct AtmosphereState {
    double pressure_pa = 0.0;
    double density_kg_m3 = 0.0;
    double temperature_c = 0.0;
};

// Result of the RK4 ballistic integration (ballistics.*)
struct TrajectorySample {
    double t_s = 0.0;
    EnuPoint position;   // relative to release point, u measured against target elevation (u=0 at target)
    EnuVector velocity;
};

struct BallisticResult {
    bool converged = false;
    double fall_time_s = 0.0;
    EnuVector impact_displacement;  // horizontal displacement (e, n) at impact, u ~ 0
    std::vector<TrajectorySample> trajectory;  // sampled points, may be empty if not requested
};

// A designated ground target (raycast.* / terrain.*)
enum class ElevationSource {
    kUnknown,
    kRawDem,        // elevation read straight off the DEM tiles
    kLrfCorrected,  // DEM + live LRF datum-bias correction (see terrain.h)
    kHomeTiedDem,   // DEM value tied to the takeoff point's measured elevation
    kFlatAtHome,    // flat-terrain fallback: target elevation assumed equal to
                    // the takeoff/home elevation (no DEM data used at all)
};

struct Target {
    bool valid = false;
    LatLonAlt geodetic;
    EnuPoint enu;
    ElevationSource elevation_source = ElevationSource::kUnknown;
    double depression_angle_rad = 0.0;  // angle below horizontal of the designating ray
    std::uint64_t designated_at_us = 0;
    bool confirmed = false;
    std::uint64_t confirmed_at_us = 0;
};

// Reason codes for a failed raycast / no-solution result, surfaced verbatim
// in the floating window and logs.
enum class RaycastFailure {
    kNone,
    kRayAboveHorizon,
    kNoTerrainIntersection,
    kNoDemTile,
    kInvalidGimbalAttitude,
};

struct RaycastResult {
    bool hit = false;
    EnuPoint point;
    LatLonAlt geodetic;
    RaycastFailure failure = RaycastFailure::kNone;
    ElevationSource elevation_source = ElevationSource::kUnknown;
    double depression_angle_rad = 0.0;
};

// Dispersion summary (dispersion.*)
struct DispersionResult {
    double p50_radial_m = 0.0;
    double p90_radial_m = 0.0;
    std::size_t samples = 0;
};

// The continuously-recomputed guidance solution, 10 Hz. Everything the UI,
// gating, and release manager need in one struct.
struct Solution {
    std::uint64_t timestamp_us = 0;
    bool has_solution = false;
    EnuPoint predicted_impact_point;
    double time_to_release_s = 0.0;   // seconds until the computed release instant, may be negative
    double miss_distance_m = 0.0;     // predicted impact vs latched target

    // Forward "impact if released NOW" prediction, computed every cycle the
    // target is latched and the aircraft state is valid. This is the only
    // meaningful release cue in a hover, where the inverse solver cannot
    // converge (spec: hovering over the target is the normal multirotor drop).
    EnuPoint impact_if_released_now;
    double miss_if_released_now_m = 0.0;
    bool forward_valid = false;
    // True when the loop is in hover mode (ground speed below the run-in
    // threshold): has_solution is then driven by forward_valid, not the
    // inverse solver, and time_to_release_s is meaningless.
    bool hover_mode = false;
    // True when the current predicted miss is inside the payload's
    // target_radius_m: manual RELEASE is honoured while this holds.
    bool release_window_open = false;
    // Steering cue: how far and on which compass bearing (0 = North, CW
    // positive) the aircraft must move so impact_if_released_now lands on
    // the target. Bearing is from impact_if_released_now to the target.
    double steer_distance_m = 0.0;
    double steer_bearing_rad = 0.0;

    BallisticResult ballistics;
    DispersionResult dispersion;
    std::string no_solution_reason;   // populated when has_solution == false
};

}  // namespace droppoint::core
