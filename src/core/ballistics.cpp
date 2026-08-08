#include "ballistics.h"

#include <array>
#include <cmath>

namespace droppoint::core::ballistics {

namespace {

struct State {
    double e, n, u;      // position, relative to release, u = height above target ground
    double ve, vn, vu;   // velocity
};

State Derivative(const State& s, double t_s, double mass_kg, const CdaAtTimeFn& cda_fn,
                  const WindAtHeightFn& wind_fn, double rho) {
    const EnuVector wind = wind_fn(s.u);
    const double rve = s.ve - wind.e;
    const double rvn = s.vn - wind.n;
    const double rvu = s.vu - wind.u;
    const double speed = std::sqrt(rve * rve + rvn * rvn + rvu * rvu);
    const double cda = cda_fn(t_s);
    const double k = (rho * cda) / (2.0 * mass_kg);

    State d{};
    d.e = s.ve;
    d.n = s.vn;
    d.u = s.vu;
    d.ve = -k * speed * rve;
    d.vn = -k * speed * rvn;
    d.vu = -kGravity - k * speed * rvu;
    return d;
}

State Rk4Step(const State& s, double t_s, double dt, double mass_kg, const CdaAtTimeFn& cda_fn,
              const WindAtHeightFn& wind_fn, double rho) {
    auto add_scaled = [](const State& a, const State& b, double scale) {
        return State{a.e + b.e * scale, a.n + b.n * scale, a.u + b.u * scale,
                      a.ve + b.ve * scale, a.vn + b.vn * scale, a.vu + b.vu * scale};
    };

    const State k1 = Derivative(s, t_s, mass_kg, cda_fn, wind_fn, rho);
    const State k2 = Derivative(add_scaled(s, k1, dt / 2.0), t_s + dt / 2.0, mass_kg, cda_fn, wind_fn, rho);
    const State k3 = Derivative(add_scaled(s, k2, dt / 2.0), t_s + dt / 2.0, mass_kg, cda_fn, wind_fn, rho);
    const State k4 = Derivative(add_scaled(s, k3, dt), t_s + dt, mass_kg, cda_fn, wind_fn, rho);

    State out;
    out.e = s.e + (dt / 6.0) * (k1.e + 2 * k2.e + 2 * k3.e + k4.e);
    out.n = s.n + (dt / 6.0) * (k1.n + 2 * k2.n + 2 * k3.n + k4.n);
    out.u = s.u + (dt / 6.0) * (k1.u + 2 * k2.u + 2 * k3.u + k4.u);
    out.ve = s.ve + (dt / 6.0) * (k1.ve + 2 * k2.ve + 2 * k3.ve + k4.ve);
    out.vn = s.vn + (dt / 6.0) * (k1.vn + 2 * k2.vn + 2 * k3.vn + k4.vn);
    out.vu = s.vu + (dt / 6.0) * (k1.vu + 2 * k2.vu + 2 * k3.vu + k4.vu);
    return out;
}

}  // namespace

CdaAtTimeFn MakeCdaProfile(const PayloadProfile& profile) {
    const double cda_free = profile.cda_free;
    const bool has_deploy = profile.cda_deployed.has_value() && profile.deploy_delay_s.has_value();
    const double cda_deployed = has_deploy ? *profile.cda_deployed : cda_free;
    const double delay = has_deploy ? *profile.deploy_delay_s : 0.0;
    return [cda_free, cda_deployed, delay, has_deploy](double t_s) {
        if (!has_deploy) return cda_free;
        return (t_s < delay) ? cda_free : cda_deployed;
    };
}

BallisticResult IntegrateForward(const ForwardInputs& in) {
    BallisticResult result;
    State s{0.0, 0.0, in.release_height_agl_m, in.initial_velocity.e, in.initial_velocity.n,
             in.initial_velocity.u};

    double t = 0.0;
    if (in.record_trajectory) {
        result.trajectory.push_back(
            TrajectorySample{t, EnuPoint{s.e, s.n, s.u}, EnuVector{s.ve, s.vn, s.vu}});
    }

    while (s.u > 0.0 && t < kMaxFlightTimeS) {
        const State next = Rk4Step(s, t, kFixedDtS, in.mass_kg, in.cda_at_time, in.wind_at_height,
                                    in.air_density_kg_m3);
        if (next.u <= 0.0) {
            // Linear interpolation between s (u>0) and next (u<=0) to find
            // the sub-step impact time, matching a bisection-free crossing
            // refinement adequate at 5 ms steps for this application.
            const double frac = s.u / (s.u - next.u);
            State impact;
            impact.e = s.e + frac * (next.e - s.e);
            impact.n = s.n + frac * (next.n - s.n);
            impact.u = 0.0;
            impact.ve = s.ve + frac * (next.ve - s.ve);
            impact.vn = s.vn + frac * (next.vn - s.vn);
            impact.vu = s.vu + frac * (next.vu - s.vu);
            t += frac * kFixedDtS;
            s = impact;
            if (in.record_trajectory) {
                result.trajectory.push_back(
                    TrajectorySample{t, EnuPoint{s.e, s.n, s.u}, EnuVector{s.ve, s.vn, s.vu}});
            }
            result.converged = true;
            break;
        }
        s = next;
        t += kFixedDtS;
        if (in.record_trajectory) {
            result.trajectory.push_back(
                TrajectorySample{t, EnuPoint{s.e, s.n, s.u}, EnuVector{s.ve, s.vn, s.vu}});
        }
    }

    result.fall_time_s = t;
    result.impact_displacement = EnuVector{s.e, s.n, s.u};
    return result;
}

double TerminalVelocity(double mass_kg, double cda_m2, double air_density_kg_m3) {
    if (cda_m2 <= 0.0 || air_density_kg_m3 <= 0.0) return 0.0;
    return std::sqrt((2.0 * mass_kg * kGravity) / (air_density_kg_m3 * cda_m2));
}

InverseResult SolveInverse(const InverseInputs& in) {
    InverseResult result;

    const double speed = std::sqrt(in.aircraft_velocity.e * in.aircraft_velocity.e +
                                     in.aircraft_velocity.n * in.aircraft_velocity.n);
    // Track direction unit vector; if the aircraft is essentially stationary,
    // fall back to the direction toward the target so the secant search has
    // a well-defined axis.
    double track_e, track_n;
    if (speed > 1e-3) {
        track_e = in.aircraft_velocity.e / speed;
        track_n = in.aircraft_velocity.n / speed;
    } else {
        const double de = in.target.e - in.aircraft_position.e;
        const double dn = in.target.n - in.aircraft_position.n;
        const double d = std::sqrt(de * de + dn * dn);
        if (d < 1e-6) {
            result.converged = false;
            return result;
        }
        track_e = de / d;
        track_n = dn / d;
    }

    auto simulate_at_tau = [&](double tau) -> std::pair<double, BallisticResult> {
        EnuPoint release{in.aircraft_position.e + in.aircraft_velocity.e * tau,
                          in.aircraft_position.n + in.aircraft_velocity.n * tau,
                          in.aircraft_position.u};
        ForwardInputs fwd;
        fwd.mass_kg = in.mass_kg;
        fwd.cda_at_time = in.cda_at_time;
        fwd.release_height_agl_m = release.u - in.target.u;
        fwd.initial_velocity = in.aircraft_velocity;
        fwd.wind_at_height = in.wind_at_height;
        fwd.air_density_kg_m3 = in.air_density_kg_m3;
        fwd.record_trajectory = false;
        BallisticResult br = IntegrateForward(fwd);

        const double impact_e = release.e + br.impact_displacement.e;
        const double impact_n = release.n + br.impact_displacement.n;
        const double err = (in.target.e - impact_e) * track_e + (in.target.n - impact_n) * track_n;
        return {err, br};
    };

    // Secant method starting from tau=0 and a small perturbation.
    double tau0 = 0.0;
    double tau1 = 1.0;
    auto [err0, br0] = simulate_at_tau(tau0);
    auto [err1, br1] = simulate_at_tau(tau1);

    int iter = 0;
    for (; iter < 10; ++iter) {
        if (std::abs(err1) < 1e-3) {
            break;
        }
        const double denom = (err1 - err0);
        if (std::abs(denom) < 1e-9) {
            break;
        }
        const double tau2 = tau1 - err1 * (tau1 - tau0) / denom;
        tau0 = tau1;
        err0 = err1;
        tau1 = tau2;
        auto [e1, b1] = simulate_at_tau(tau1);
        err1 = e1;
        br1 = b1;
    }

    result.iterations = iter + 1;
    if (std::abs(err1) < 0.05) {  // converged to within 5 cm along-track
        result.converged = true;
        result.release_along_track_distance_m = tau1 * speed;
        result.release_point = EnuPoint{in.aircraft_position.e + in.aircraft_velocity.e * tau1,
                                          in.aircraft_position.n + in.aircraft_velocity.n * tau1,
                                          in.aircraft_position.u};
        result.ballistics_at_release = br1;
    } else {
        result.converged = false;
    }
    return result;
}

}  // namespace droppoint::core::ballistics
