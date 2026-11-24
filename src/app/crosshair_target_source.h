// app/crosshair_target_source.h
//
// Source B: gimbal crosshair designation. Uses the live aircraft attitude,
// gimbal angles, lens intrinsics, and DEM to raycast the centre of the FOV.
#pragma once

#include <functional>
#include <string>

#include "core/projection.h"
#include "core/raycast.h"
#include "core/solution.h"
#include "core/terrain.h"
#include "hal/i_gimbal.h"
#include "hal/i_rangefinder.h"
#include "hal/i_target_source.h"
#include "hal/i_telemetry.h"

namespace droppoint::app {

class CrosshairTargetSource : public hal::ITargetSource {
public:
    CrosshairTargetSource(hal::ITelemetry& telemetry, hal::IGimbal& gimbal,
                          const core::projection::Intrinsics& intrinsics,
                          core::geodesy::EnuOrigin origin,
                          core::terrain::DemSampleFn dem_fn,
                          std::function<core::ElevationSource(double lat_deg, double lon_deg)>
                              elevation_source_fn = {});

    std::string Name() const override { return "gimbal_crosshair"; }
    bool IsAvailable() const override;
    std::optional<hal::DesignationOutcome> TryDesignate(const hal::DesignationRequest& request) override;

private:
    hal::ITelemetry& telemetry_;
    hal::IGimbal& gimbal_;
    core::projection::Intrinsics intrinsics_;
    core::geodesy::EnuOrigin origin_;
    core::terrain::DemSampleFn dem_fn_;
    // Optional refinement of the raycast's elevation source: when the
    // raycast reports kRawDem (no LRF bias applied) this reports the more
    // precise source the DEM backend actually used (kHomeTiedDem /
    // kRawDem / kFlatAtHome / kUnknown). kLrfCorrected is never overridden.
    std::function<core::ElevationSource(double, double)> elevation_source_fn_;
};

}  // namespace droppoint::app
