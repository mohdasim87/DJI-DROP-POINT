// app/crosshair_target_source.cpp
//
// Source B: gimbal crosshair designation. Status: unverified.
#include "crosshair_target_source.h"

#include "core/geodesy.h"
#include "core/raycast.h"

namespace droppoint::app {

CrosshairTargetSource::CrosshairTargetSource(hal::ITelemetry& telemetry, hal::IGimbal& gimbal,
                                             const core::projection::Intrinsics& intrinsics,
                                             core::geodesy::EnuOrigin origin,
                                             core::terrain::DemSampleFn dem_fn)
    : telemetry_(telemetry),
      gimbal_(gimbal),
      intrinsics_(intrinsics),
      origin_(std::move(origin)),
      dem_fn_(std::move(dem_fn)) {}

bool CrosshairTargetSource::IsAvailable() const {
    const auto aircraft = telemetry_.GetAircraftState();
    const auto gimbal = gimbal_.GetAttitude();
    return aircraft.valid && gimbal.valid;
}

std::optional<hal::DesignationOutcome> CrosshairTargetSource::TryDesignate(
    const hal::DesignationRequest& request) {
    if (!request.requested) return std::nullopt;

    hal::DesignationOutcome outcome{};

    const core::AircraftState aircraft = telemetry_.GetAircraftState();
    const core::GimbalState gimbal_state = gimbal_.GetAttitude();
    if (!aircraft.valid || !gimbal_state.valid) {
        outcome.failure_reason = "no valid aircraft/gimbal attitude";
        return outcome;
    }

    core::raycast::GimbalAttitude gimbal;
    gimbal.yaw_rad = gimbal_state.yaw_rad;
    gimbal.pitch_rad = gimbal_state.pitch_rad;
    gimbal.roll_rad = gimbal_state.roll_rad;

    core::raycast::RaycastInputs in{};
    in.aircraft_position = aircraft.position;
    in.gimbal = gimbal;
    in.intrinsics = intrinsics_;
    in.zoom_ratio = gimbal_state.zoom_ratio;
    in.origin = origin_;
    in.dem_fn = dem_fn_;
    in.u_px = static_cast<double>(intrinsics_.image_width_px) / 2.0;
    in.v_px = static_cast<double>(intrinsics_.image_height_px) / 2.0;
    in.horizon_reject_threshold = -1e-6;  // strictly downward

    const core::RaycastResult result = core::raycast::CastPixelToGround(in);
    if (!result.hit) {
        outcome.failure_reason = "raycast failed: " + std::string(core::raycast::FailureToString(result.failure));
        return outcome;
    }

    outcome.target.valid = true;
    outcome.target.geodetic = result.geodetic;
    outcome.target.enu = result.point;
    outcome.target.elevation_source = core::ElevationSource::kRawDem;
    outcome.target.depression_angle_rad = result.depression_angle_rad;
    outcome.target.designated_at_us = request.timestamp_us;
    outcome.succeeded = true;
    return outcome;
}

}  // namespace droppoint::app
