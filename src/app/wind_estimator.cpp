#include "wind_estimator.h"

#include <cmath>

#include "core/geodesy.h"
#include "core/wind.h"

namespace droppoint::app {

namespace {

bool AllFinite(const core::AircraftState& s) {
    const double values[] = {s.position.e,  s.position.n, s.position.u, s.velocity.e,
                             s.velocity.n,  s.velocity.u, s.yaw_rad,    s.pitch_rad,
                             s.roll_rad};
    for (const double v : values) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

// Two consecutive samples farther apart than this belong to different
// hover segments: a gap means the aircraft was above the speed/acceleration
// acceptance thresholds in between. Nominal sampling is the 10 Hz loop
// tick (0.1 s).
constexpr double kMaxSampleGapS = 0.3;

}  // namespace

void WindEstimator::Update(const core::AircraftState& state, double air_density_kg_m3,
                           double accel_mps2, std::uint64_t now_us) {
    // Clock went backwards (restart, time-base glitch): anything buffered is
    // from the old epoch and must not be averaged with new samples.
    if (!samples_.empty() && now_us < samples_.back().timestamp_us) {
        samples_.clear();
        archived_.clear();
    }

    const double ground_speed =
        std::sqrt(state.velocity.e * state.velocity.e + state.velocity.n * state.velocity.n);
    if (!state.valid || !AllFinite(state) || !std::isfinite(accel_mps2) ||
        !std::isfinite(air_density_kg_m3) || ground_speed >= config_.max_ground_speed_mps ||
        std::fabs(accel_mps2) >= config_.max_accel_mps2) {
        return;
    }

    core::wind::HoverEstimateInputs in;
    in.pitch_rad = state.pitch_rad;
    in.roll_rad = state.roll_rad;
    in.yaw_rad = state.yaw_rad;
    in.mass_kg = config_.total_mass_kg;
    in.cda_airframe = config_.cda_airframe;
    in.air_density_kg_m3 = air_density_kg_m3;
    const core::WindSample estimate = core::wind::EstimateFromHover(in);
    if (!estimate.valid || !std::isfinite(estimate.speed_mps) ||
        !std::isfinite(estimate.from_bearing_rad)) {
        return;
    }

    // A gap since the previous accepted sample starts a new hover segment.
    // The finished segment is archived when it is a real hover — a short
    // dip (e.g. the turnaround onto the run-in) is just discarded, so a
    // transient can never destroy or corrupt a good estimate.
    if (!samples_.empty() &&
        now_us - samples_.back().timestamp_us >
            static_cast<std::uint64_t>(kMaxSampleGapS * 1e6)) {
        if (CoverageUs(samples_) >= config_.min_hover_s * 1e6) {
            archived_ = samples_;
        }
        samples_.clear();
    }

    const core::EnuVector toward = core::wind::ToVector(estimate.speed_mps, estimate.from_bearing_rad);
    Sample s;
    s.timestamp_us = now_us;
    s.toward_e = toward.e;
    s.toward_n = toward.n;
    s.speed_mps = estimate.speed_mps;
    s.u_m = state.position.u;
    samples_.push_back(s);
    Prune();
}

std::uint64_t WindEstimator::CoverageUs(const std::deque<Sample>& seg) {
    if (seg.size() < 2) return 0;
    return seg.back().timestamp_us - seg.front().timestamp_us;
}

void WindEstimator::Prune() {
    if (samples_.empty()) return;
    // Rolling window inside the current segment only.
    const std::uint64_t newest = samples_.back().timestamp_us;
    const std::uint64_t window_us = static_cast<std::uint64_t>(config_.window_s * 1e6);
    while (samples_.size() > 1 && newest - samples_.front().timestamp_us > window_us) {
        samples_.pop_front();
    }
}

// The segment the estimate is reported from: the current one once it is a
// real hover (>= min_hover_s covered and <= max_age_s old); otherwise the
// archived previous hover while it is still fresh enough. A still-
// collecting new segment does not disqualify the last good one — a wind
// estimate a few seconds old is far better than none, and max_age_s is the
// real staleness bound.
const std::deque<WindEstimator::Sample>* WindEstimator::ReportedSegment(
    std::uint64_t now_us) const {
    const std::uint64_t min_hover_us = static_cast<std::uint64_t>(config_.min_hover_s * 1e6);
    const std::uint64_t max_age_us = static_cast<std::uint64_t>(config_.max_age_s * 1e6);
    const std::deque<Sample>* cands[2] = {&samples_, &archived_};
    for (const auto* seg : cands) {
        if (CoverageUs(*seg) < min_hover_us) continue;
        const std::uint64_t newest = seg->back().timestamp_us;
        const std::uint64_t age_us = now_us >= newest ? now_us - newest : 0;
        if (age_us <= max_age_us) return seg;
    }
    return nullptr;
}

core::WindSample WindEstimator::Current(std::uint64_t now_us) const {
    core::WindSample out;
    out.from_telemetry_topic = false;
    const auto* seg = ReportedSegment(now_us);
    if (seg == nullptr) return out;

    double sum_e = 0.0, sum_n = 0.0;
    for (const Sample& s : *seg) {
        sum_e += s.toward_e;
        sum_n += s.toward_n;
    }
    const double mean_e = sum_e / seg->size();
    const double mean_n = sum_n / seg->size();
    out.speed_mps = std::sqrt(mean_e * mean_e + mean_n * mean_n);
    // Mean "toward" vector -> reciprocal of its compass bearing is the
    // meteorological "from" bearing.
    out.from_bearing_rad =
        core::geodesy::NormalizeCompassBearingRad(std::atan2(-mean_e, -mean_n));
    out.valid = true;
    return out;
}

double WindEstimator::GustSigmaMps() const {
    // Same segment Current() reports, evaluated as of the newest sample.
    const std::uint64_t now = samples_.empty() ? 0 : samples_.back().timestamp_us;
    const auto* seg = ReportedSegment(now);
    if (seg == nullptr || seg->size() < 2) return 0.0;
    double sum = 0.0, sum_sq = 0.0;
    for (const Sample& s : *seg) {
        sum += s.speed_mps;
        sum_sq += s.speed_mps * s.speed_mps;
    }
    const double mean = sum / seg->size();
    const double var = sum_sq / seg->size() - mean * mean;
    return var > 0.0 ? std::sqrt(var) : 0.0;
}

double WindEstimator::ReferenceU() const {
    const std::uint64_t now = samples_.empty() ? 0 : samples_.back().timestamp_us;
    const auto* seg = ReportedSegment(now);
    if (seg == nullptr) return 0.0;
    double sum = 0.0;
    for (const Sample& s : *seg) sum += s.u_m;
    return sum / seg->size();
}

double WindEstimator::AgeS(std::uint64_t now_us) const {
    const std::uint64_t now = samples_.empty() ? 0 : samples_.back().timestamp_us;
    const auto* seg = ReportedSegment(now);
    if (seg == nullptr) return 1e9;
    const std::uint64_t newest = seg->back().timestamp_us;
    return (now_us >= newest) ? (now_us - newest) / 1e6 : 0.0;
}

}  // namespace droppoint::app
