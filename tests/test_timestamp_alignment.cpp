// Spec section 14: "Timestamp alignment test: scripted yaw with known video
// delay, projected marker on a static ground point must not move beyond a
// stated pixel tolerance." This exercises util::RingBuffer's interpolation
// used to match a video frame's capture time to the correct pose sample
// (spec 6.7), independent of any HAL/hardware dependency.
#include <cmath>

#include "core/math_constants.h"
#include "core/projection.h"
#include "core/raycast.h"
#include "test_framework.h"
#include "util/ring_buffer.h"

using namespace droppoint::core;
using namespace droppoint::util;

namespace {
struct PoseSample {
    std::uint64_t timestamp_us;
    double gimbal_yaw_rad;
};
}  // namespace

TEST_CASE("Timestamp alignment: interpolated pose keeps a static marker within tolerance") {
    // A scripted, steady yaw sweep sampled at 50 Hz (20ms), matching the
    // spec's "50 Hz or better" ring buffer requirement.
    RingBuffer<PoseSample> buffer(200);
    constexpr double kYawRateRadPerSec = 0.2;  // slow, steady sweep
    for (int i = 0; i < 150; ++i) {
        const std::uint64_t t_us = static_cast<std::uint64_t>(i) * 20000;  // 20 ms steps = 50 Hz
        buffer.Push(PoseSample{t_us, kYawRateRadPerSec * (t_us / 1e6)});
    }

    // A known, fixed video-to-telemetry delay: the frame's *reported*
    // capture time lags the pose stream by this much, so the correct pose
    // to use is the interpolated one at (frame_time - delay), not the most
    // recent pose sample.
    constexpr std::uint64_t kVideoDelayUs = 45000;  // 45 ms, a plausible USB/encode pipeline delay
    constexpr std::uint64_t kFrameTimeUs = 2'000'000;  // an arbitrary frame well into the sweep

    const std::uint64_t query_time = kFrameTimeUs - kVideoDelayUs;
    const auto bracket = buffer.FindBracket(query_time, [](const PoseSample& s) { return s.timestamp_us; });
    REQUIRE(bracket.has_value());

    const auto& [older, newer] = *bracket;
    const double frac = (newer.timestamp_us == older.timestamp_us)
                             ? 0.0
                             : static_cast<double>(query_time - older.timestamp_us) /
                                   static_cast<double>(newer.timestamp_us - older.timestamp_us);
    const double interpolated_yaw = older.gimbal_yaw_rad + frac * (newer.gimbal_yaw_rad - older.gimbal_yaw_rad);
    const double true_yaw_at_query_time = kYawRateRadPerSec * (query_time / 1e6);

    // The interpolation itself should recover the true yaw at the
    // delay-corrected query time to well under a pixel's worth of angle at
    // typical zoom/FOV (a few hundredths of a degree, given 50 Hz sampling
    // and a slow sweep).
    REQUIRE_NEAR(interpolated_yaw, true_yaw_at_query_time, 1e-4);

    // Project a static ground point through both the correctly-interpolated
    // pose and the naive "most recent sample" pose, and confirm the
    // correctly-interpolated projection stays far closer to the true
    // marker position -- this is the failure mode the spec calls out
    // ("almost every unstable overlay is a timestamp failure").
    projection::Intrinsics intr;
    intr.image_width_px = 1920;
    intr.image_height_px = 1080;
    intr.hfov_rad = 60.0 * kPi / 180.0;
    intr.vfov_rad = 35.0 * kPi / 180.0;
    intr.c_x = 960;
    intr.c_y = 540;

    auto project_at_yaw = [&](double yaw_rad) {
        raycast::GimbalAttitude gimbal{yaw_rad, -kPi / 3.0, 0.0};
        // A fixed ground point straight ahead-ish of the aircraft.
        const EnuVector basis_z = raycast::CameraRayToEnu(projection::CameraPoint{0, 0, 1}, gimbal);
        const EnuVector basis_x = raycast::CameraRayToEnu(projection::CameraPoint{1, 0, 0}, gimbal);
        const EnuVector basis_y = raycast::CameraRayToEnu(projection::CameraPoint{0, 1, 0}, gimbal);
        const EnuVector dir{20.0, 5.0, -50.0};  // arbitrary fixed direction to a ground feature
        const double cx = dir.e * basis_x.e + dir.n * basis_x.n + dir.u * basis_x.u;
        const double cy = dir.e * basis_y.e + dir.n * basis_y.n + dir.u * basis_y.u;
        const double cz = dir.e * basis_z.e + dir.n * basis_z.n + dir.u * basis_z.u;
        return projection::ProjectToPixel(projection::CameraPoint{cx, cy, cz}, intr, 1.0);
    };

    const double most_recent_sample_yaw = buffer.At(0).gimbal_yaw_rad;  // naive: ignore the video delay entirely
    const auto truth = project_at_yaw(true_yaw_at_query_time);
    const auto corrected = project_at_yaw(interpolated_yaw);
    const auto naive = project_at_yaw(most_recent_sample_yaw);

    REQUIRE(truth.visible);
    REQUIRE(corrected.visible);
    REQUIRE(naive.visible);

    const double corrected_error_px =
        std::hypot(corrected.u_px - truth.u_px, corrected.v_px - truth.v_px);
    const double naive_error_px = std::hypot(naive.u_px - truth.u_px, naive.v_px - truth.v_px);

    REQUIRE(corrected_error_px < 1.0);          // stated pixel tolerance for the corrected path
    REQUIRE(naive_error_px > corrected_error_px);  // demonstrates the failure mode the correction fixes
}
