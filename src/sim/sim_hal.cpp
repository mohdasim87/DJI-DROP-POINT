#include "sim_hal.h"

#include "core/geodesy.h"
#include "core/wind.h"

namespace droppoint::sim {

core::AircraftState SimTelemetry::GetAircraftState() {
    const auto reading = world_->LatestTelemetry();
    if (reading.has_value()) {
        last_valid_ = *reading;
        last_update_us_ = world_->NowUs();
        if (callback_) callback_(last_valid_);
    }
    return last_valid_;
}

hal::GnssStatus SimTelemetry::GetGnssStatus() {
    hal::GnssStatus status;
    status.satellite_count = world_->SimulatedSatelliteCount();
    status.rtk_connected = true;
    status.rtk_status = world_->SimulatedRtkFix() ? hal::RtkFixStatus::kFix : hal::RtkFixStatus::kFloat;
    return status;
}

double SimTelemetry::GetBarometricAltitudeM() { return last_valid_.position.u; }

std::uint64_t SimTelemetry::GetTelemetryAgeUs() {
    return world_->NowUs() >= last_update_us_ ? (world_->NowUs() - last_update_us_) : 0;
}

core::WindSample SimTelemetry::GetWind() {
    // No PSDK wind topic exists (docs/OPEN-QUESTIONS.md #2); the simulator
    // mirrors that by only ever exposing the hover-estimate fallback,
    // computed from the ground-truth wind rather than aircraft attitude
    // (the sim's aircraft never actually tilts to hold position), which is
    // sufficient to exercise the guidance loop's consumption of wind data.
    core::WindSample sample;
    const core::EnuVector w = world_->WindAtHeight(std::max(0.5, last_valid_.position.u));
    sample.speed_mps = std::sqrt(w.e * w.e + w.n * w.n);
    sample.from_bearing_rad = core::geodesy::WrapAngleRad(std::atan2(-w.e, -w.n));
    sample.from_telemetry_topic = false;
    sample.valid = true;
    return sample;
}

std::vector<hal::RcButtonEvent> SimTelemetry::PollRcButtonEvents() {
    auto out = pending_buttons_;
    pending_buttons_.clear();
    return out;
}

void SimTelemetry::InjectButton(hal::RcButton button) {
    pending_buttons_.push_back(hal::RcButtonEvent{button, world_->NowUs()});
}

std::optional<hal::RangefinderReading> SimRangefinder::GetLatestReading() {
    if (!measurement_requested_) return std::nullopt;
    measurement_requested_ = false;

    hal::RangefinderReading reading;
    reading.valid = true;
    reading.geodetic = world_->CurrentGeodetic();
    reading.geodetic.alt_m_hae = world_->GroundElevationAt(reading.geodetic.lat_deg, reading.geodetic.lon_deg);
    reading.slant_range_m = 50.0;
    reading.timestamp_us = world_->NowUs();
    return reading;
}

void SimRelease::Fire() {
    if (state_ != hal::ReleaseState::kSafe) return;  // single-shot: ignore re-entrant calls
    state_ = hal::ReleaseState::kFiring;
    ++fire_count_;
    state_ = hal::ReleaseState::kFired;
}

void SimRelease::SetSafe() { state_ = hal::ReleaseState::kSafe; }

std::optional<hal::DesignationOutcome> SimCrosshairTargetSource::TryDesignate(
    const hal::DesignationRequest& request) {
    if (!request.requested) return std::nullopt;

    const core::GimbalState gimbal = world_->GimbalAttitude();
    const core::AircraftState aircraft = *world_->LatestTelemetry();

    const auto origin = core::geodesy::MakeEnuOrigin(core::LatLonAlt{47.6062, -122.3321, 30.0});

    core::raycast::RaycastInputs in;
    in.u_px = intrinsics_.c_x;
    in.v_px = intrinsics_.c_y;
    in.intrinsics = intrinsics_;
    in.zoom_ratio = gimbal.zoom_ratio;
    in.gimbal = core::raycast::GimbalAttitude{gimbal.yaw_rad, gimbal.pitch_rad, gimbal.roll_rad};
    in.aircraft_position = aircraft.position;
    in.origin = origin;
    in.dem_fn = [this](double lat, double lon) -> std::optional<double> {
        return world_->GroundElevationAt(lat, lon);
    };
    in.now_us = world_->NowUs();

    const core::RaycastResult result = core::raycast::CastPixelToGround(in);

    hal::DesignationOutcome outcome;
    if (!result.hit) {
        outcome.succeeded = false;
        outcome.failure_reason = "raycast failed to intersect terrain";
        return outcome;
    }

    outcome.succeeded = true;
    outcome.target.valid = true;
    outcome.target.enu = result.point;
    outcome.target.geodetic = result.geodetic;
    outcome.target.elevation_source = result.elevation_source;
    outcome.target.depression_angle_rad = result.depression_angle_rad;
    outcome.target.designated_at_us = request.timestamp_us;
    return outcome;
}

}  // namespace droppoint::sim
