// core/ballistics.h
//
// RK4 ballistic integrator (forward and inverse modes) plus terminal
// velocity helper. See spec section 6.4.
#pragma once

#include <functional>

#include "solution.h"

namespace droppoint::core::ballistics {

inline constexpr double kGravity = 9.80665;      // m/s^2
inline constexpr double kFixedDtS = 0.005;        // 5 ms, per spec
inline constexpr double kMaxFlightTimeS = 60.0;

// Wind as a function of height-above-ground (metres, u=0 at target
// elevation). Callers build this from wind::SpeedAtHeight + wind::ToVector.
using WindAtHeightFn = std::function<EnuVector(double height_agl_m)>;

// Drag area (m^2) as a function of elapsed time since release, to support
// two-phase (streamer) profiles: free CdA until deployment delay, then
// deployed CdA.
using CdaAtTimeFn = std::function<double(double t_s)>;

CdaAtTimeFn MakeCdaProfile(const PayloadProfile& profile);

struct ForwardInputs {
    double mass_kg = 0.0;
    CdaAtTimeFn cda_at_time;
    double release_height_agl_m = 0.0;  // height above TARGET ground elevation
    EnuVector initial_velocity;         // payload velocity at release (== aircraft velocity)
    WindAtHeightFn wind_at_height;
    double air_density_kg_m3 = 1.225;   // evaluated at release; core does not re-derive per-altitude density
    bool record_trajectory = false;
};

// Integrates the payload from release to ground impact (u <= 0), RK4, fixed
// dt = 5 ms, terminating at t > 60 s if impact is never reached (converged
// == false in that case).
BallisticResult IntegrateForward(const ForwardInputs& in);

// Given a latched target (height-above-ground = 0 by definition, at some
// horizontal offset from the aircraft) and the aircraft's current track
// (position + velocity, assumed to continue in a straight line at constant
// height and speed), finds the along-track release position such that the
// forward integration lands on the target. Secant iteration, must converge
// within 10 iterations or report no-solution.
struct InverseInputs {
    double mass_kg = 0.0;
    CdaAtTimeFn cda_at_time;
    EnuPoint aircraft_position;   // current ENU position
    EnuVector aircraft_velocity;  // current ENU velocity (assumed to persist along track)
    EnuPoint target;              // latched target, ENU, u == target ground elevation frame (0)
    WindAtHeightFn wind_at_height;
    double air_density_kg_m3 = 1.225;
};

struct InverseResult {
    bool converged = false;
    double release_along_track_distance_m = 0.0;  // signed distance from current position to release point
    EnuPoint release_point;
    BallisticResult ballistics_at_release;
    int iterations = 0;
};
InverseResult SolveInverse(const InverseInputs& in);

// Terminal velocity for a body with the given mass, drag area and ambient
// density: v = sqrt(2*m*g / (rho*CdA)).
double TerminalVelocity(double mass_kg, double cda_m2, double air_density_kg_m3);

}  // namespace droppoint::core::ballistics
