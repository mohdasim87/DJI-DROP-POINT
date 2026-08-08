#include "terrain.h"

#include <cmath>

#include "geodesy.h"

namespace droppoint::core::terrain {

void DatumBias::Update(double p_lrf_z, double dem_elevation_at_fix, std::uint64_t timestamp_us,
                        const EnuPoint& aircraft_position_at_fix) {
    bias_m_ = p_lrf_z - dem_elevation_at_fix;
    set_at_us_ = timestamp_us;
    position_at_set_ = aircraft_position_at_fix;
    has_bias_ = true;
}

bool DatumBias::IsValid(std::uint64_t now_us, const EnuPoint& current_aircraft_position) const {
    if (!has_bias_) return false;
    const double age_s = (now_us >= set_at_us_) ? (now_us - set_at_us_) / 1e6 : 0.0;
    if (age_s > kBiasExpirySeconds) return false;
    const double moved_m = geodesy::HorizontalRange(position_at_set_, current_aircraft_position);
    if (moved_m > kBiasExpiryDistanceM) return false;
    return true;
}

void DatumBias::Clear() { has_bias_ = false; }

ElevationResult QueryElevation(double lat_deg, double lon_deg, const DemSampleFn& dem_fn,
                                const DatumBias& bias, std::uint64_t now_us,
                                const EnuPoint& current_aircraft_position) {
    ElevationResult result;
    const std::optional<double> raw = dem_fn(lat_deg, lon_deg);
    if (!raw.has_value()) {
        result.available = false;
        return result;
    }

    result.available = true;
    if (bias.IsValid(now_us, current_aircraft_position)) {
        result.elevation_m = *raw + bias.BiasMetres();
        result.source = ElevationSource::kLrfCorrected;
    } else {
        result.elevation_m = *raw;
        result.source = ElevationSource::kRawDem;
    }
    return result;
}

}  // namespace droppoint::core::terrain
