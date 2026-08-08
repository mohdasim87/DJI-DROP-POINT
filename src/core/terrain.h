// core/terrain.h
//
// DEM elevation lookup plus the LRF datum-bias correction (spec 3.4 step 7).
// core/ performs no I/O itself: DEM sampling is injected as a callback so
// the actual file/tile access lives in app/ or hal/, keeping this file
// structs-in/structs-out and unit-testable with a fake DEM function.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "solution.h"

namespace droppoint::core::terrain {

// Returns the raw DEM elevation (metres, WGS-84 HAE or whatever datum the
// DEM tiles use, consistently) at a lat/lon, or nullopt if no tile covers
// that location.
using DemSampleFn = std::function<std::optional<double>(double lat_deg, double lon_deg)>;

inline constexpr double kBiasExpirySeconds = 120.0;
inline constexpr double kBiasExpiryDistanceM = 200.0;

// Tracks the current LRF-derived datum bias and its validity window.
class DatumBias {
public:
    // Applies a new LRF fix: bias = P_lrf.z - DEM_elevation(P_lrf.lat, P_lrf.lon).
    // `dem_elevation_at_fix` is the raw DEM lookup at the LRF point, `p_lrf_z`
    // is the elevation implied by the aircraft position + LRF slant range
    // (see raycast.h step 7).
    void Update(double p_lrf_z, double dem_elevation_at_fix, std::uint64_t timestamp_us,
                const EnuPoint& aircraft_position_at_fix);

    // True if a bias exists and has not expired (120 s or 200 m of aircraft
    // movement since it was set, per spec).
    bool IsValid(std::uint64_t now_us, const EnuPoint& current_aircraft_position) const;

    double BiasMetres() const { return bias_m_; }
    void Clear();

private:
    bool has_bias_ = false;
    double bias_m_ = 0.0;
    std::uint64_t set_at_us_ = 0;
    EnuPoint position_at_set_;
};

struct ElevationResult {
    bool available = false;
    double elevation_m = 0.0;
    ElevationSource source = ElevationSource::kUnknown;
};

// Looks up terrain elevation at (lat, lon), applying the datum bias if
// valid. Falls back to raw DEM (source = kRawDem) if the bias has expired
// or was never set; reports kLrfCorrected when applied.
ElevationResult QueryElevation(double lat_deg, double lon_deg, const DemSampleFn& dem_fn,
                                const DatumBias& bias, std::uint64_t now_us,
                                const EnuPoint& current_aircraft_position);

}  // namespace droppoint::core::terrain
