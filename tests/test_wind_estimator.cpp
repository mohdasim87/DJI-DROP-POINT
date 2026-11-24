#include <cmath>
#include <limits>

#include "app/wind_estimator.h"
#include "core/ballistics.h"
#include "core/geodesy.h"
#include "core/math_constants.h"
#include "core/wind.h"
#include "test_framework.h"

using namespace droppoint;

namespace {

app::WindEstimator MakeEstimator() {
    app::WindEstimatorConfig cfg;
    cfg.total_mass_kg = 6.3;
    cfg.cda_airframe = 0.45;
    return app::WindEstimator(cfg);
}

// Emulates the telemetry attitude of a multirotor holding a hover in a
// steady wind, using the same tilt law the simulator applies:
//   tan(theta) = rho*CdA*V^2 / (2 m g), leaning toward the from-bearing.
core::AircraftState HoverState(double wind_speed, double wind_from_rad, double heading_math_rad,
                                double mass_kg, double cda, double rho, std::uint64_t t_us) {
    const double theta =
        std::atan(rho * cda * wind_speed * wind_speed / (2.0 * mass_kg * core::ballistics::kGravity));
    const double heading_compass = core::geodesy::WrapAngleRad(core::kPi / 2.0 - heading_math_rad);
    const double delta = wind_from_rad - heading_compass;

    core::AircraftState s;
    s.timestamp_us = t_us;
    s.position = {0, 0, 60.0};
    s.velocity = {0, 0, 0};
    s.yaw_rad = heading_math_rad;
    s.pitch_rad = -theta * std::cos(delta);
    s.roll_rad = theta * std::sin(delta);
    s.valid = true;
    return s;
}

void FeedHover(app::WindEstimator& est, double wind_speed, double wind_from_rad,
               double duration_s, std::uint64_t start_us, double mass_kg = 6.3, double cda = 0.45,
               double rho = 1.225) {
    for (std::uint64_t t = start_us; t < start_us + static_cast<std::uint64_t>(duration_s * 1e6);
         t += 100000) {
        est.Update(HoverState(wind_speed, wind_from_rad, core::kPi / 2.0, mass_kg, cda, rho, t), rho,
                   0.0, t);
    }
}

}  // namespace

TEST_CASE("WindEstimator: no estimate before min_hover_s") {
    auto est = MakeEstimator();
    FeedHover(est, 5.0, 0.0, 1.0, 0);  // 1 s < 3 s minimum
    const core::WindSample w = est.Current(2'000'000);
    REQUIRE(!w.valid);
}

TEST_CASE("WindEstimator: recovers a steady 5 m/s wind from 270") {
    auto est = MakeEstimator();
    FeedHover(est, 5.0, 270.0 * core::kPi / 180.0, 5.0, 0);
    const core::WindSample w = est.Current(6'000'000);
    REQUIRE(w.valid);
    REQUIRE(!w.from_telemetry_topic);
    REQUIRE_REL_NEAR(w.speed_mps, 5.0, 0.02);
    REQUIRE_NEAR(w.from_bearing_rad, 270.0 * core::kPi / 180.0, 2.0 * core::kPi / 180.0);
}

TEST_CASE("WindEstimator: samples ignored above 0.5 m/s ground speed") {
    auto est = MakeEstimator();
    // Feed 5 s of hover-like attitudes but moving at 1 m/s: rejected.
    for (std::uint64_t t = 0; t < 5'000'000; t += 100000) {
        auto s = HoverState(5.0, 0.0, core::kPi / 2.0, 6.3, 0.45, 1.225, t);
        s.velocity = {1.0, 0.0, 0.0};
        est.Update(s, 1.225, 0.0, t);
    }
    REQUIRE(!est.Current(6'000'000).valid);

    // High acceleration also rejects samples.
    for (std::uint64_t t = 5'000'000; t < 10'000'000; t += 100000) {
        est.Update(HoverState(5.0, 0.0, core::kPi / 2.0, 6.3, 0.45, 1.225, t), 1.225, 2.0, t);
    }
    REQUIRE(!est.Current(11'000'000).valid);
}

TEST_CASE("WindEstimator: estimate expires after max_age_s") {
    auto est = MakeEstimator();
    FeedHover(est, 5.0, 0.0, 5.0, 0);
    REQUIRE(est.Current(10'000'000).valid);
    REQUIRE(!est.Current(310'000'000).valid);  // newest sample ~5 s old + 300 s max age
}

TEST_CASE("WindEstimator: gust sigma zero for steady wind, positive for varying") {
    auto est = MakeEstimator();
    FeedHover(est, 5.0, 0.0, 5.0, 0);
    REQUIRE_NEAR(est.GustSigmaMps(), 0.0, 1e-6);

    app::WindEstimator gusty = MakeEstimator();
    for (std::uint64_t t = 0; t < 5'000'000; t += 100000) {
        const double v = 5.0 + 2.0 * std::sin(2.0 * core::kPi * 0.5 * (t / 1e6));
        gusty.Update(HoverState(v, 0.0, core::kPi / 2.0, 6.3, 0.45, 1.225, t), 1.225, 0.0, t);
    }
    REQUIRE(gusty.GustSigmaMps() > 0.5);
}

TEST_CASE("WindEstimator: NaN inputs and backwards time are handled") {
    auto est = MakeEstimator();
    FeedHover(est, 5.0, 0.0, 5.0, 0);
    REQUIRE(est.Current(6'000'000).valid);

    // NaN attitude is ignored, not added and not crashing.
    core::AircraftState bad = HoverState(5.0, 0.0, 0.0, 6.3, 0.45, 1.225, 6'000'000);
    bad.pitch_rad = std::numeric_limits<double>::quiet_NaN();
    est.Update(bad, 1.225, 0.0, 6'000'000);
    REQUIRE(est.Current(6'100'000).valid);

    // Clock going backwards clears the window without crashing.
    est.Update(HoverState(5.0, 0.0, 0.0, 6.3, 0.45, 1.225, 1'000'000), 1.225, 0.0, 1'000'000);
    REQUIRE(!est.Current(1'100'000).valid);
}

TEST_CASE("WindEstimator: ReferenceU reports mean hover altitude") {
    auto est = MakeEstimator();
    for (std::uint64_t t = 0; t < 5'000'000; t += 100000) {
        auto s = HoverState(5.0, 0.0, 0.0, 6.3, 0.45, 1.225, t);
        s.position.u = 55.0;
        est.Update(s, 1.225, 0.0, t);
    }
    REQUIRE_NEAR(est.ReferenceU(), 55.0, 1e-6);
    REQUIRE(est.AgeS(5'500'000) > 0.0);
}
