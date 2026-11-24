// hw/terrain_provider.h
//
// SRTM .hgt DEM reader + elevation-source policy (spec section 3.4,
// docs/CZI-DROP-KITS.md facts):
//
//   * SRTM heights are EGM96 mean-sea-level; aircraft altitude is WGS-84
//     ellipsoid. A raw tile must therefore never be used without a datum
//     tie: the difference is the local geoid undulation (tens of metres).
//   * SetHomeTie() pins the DEM to the aircraft-measured ground altitude at
//     the takeoff point; Elevation() then returns DEM + offset.
//   * With a tile but NO home tie, elevations are refused (nullopt) unless
//     system.yaml sets terrain.allow_raw_dem.
//   * With NO tile but a home tie and allow_flat_terrain_fallback, the
//     target elevation falls back to the takeoff ground altitude.
//
// Tile format: big-endian int16 metres, 1201^2 (SRTM3) or 3601^2 (SRTM1),
// files named e.g. N34E073.hgt under dem_directory. -32768 = void.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/solution.h"

namespace droppoint::hw {

struct TerrainProviderConfig {
    std::string dem_directory;
    bool allow_raw_dem = false;               // system.yaml terrain.allow_raw_dem
    bool allow_flat_terrain_fallback = true;  // system.yaml allow_flat_terrain_fallback
};

class TerrainProvider {
public:
    explicit TerrainProvider(TerrainProviderConfig config);

    // Records the aircraft-measured ground altitude at the home point
    // (lat/lon, WGS-84 ellipsoid metres). While a DEM tile covers the home
    // point the provider computes offset = ground_alt - dem(home) and
    // applies it to every elevation read; the tie itself is recorded either
    // way (needed for the flat-terrain fallback).
    void SetHomeTie(double lat_deg, double lon_deg, double ground_alt_ellipsoid_m);
    bool HasHomeTie() const { return has_home_tie_; }
    double HomeTieAltitudeM() const { return home_ground_alt_m_; }

    // Terrain elevation at (lat, lon) in the WGS-84-ellipsoid-compatible
    // frame the rest of the system uses, or nullopt when no elevation is
    // usable (see the header comment for the source policy).
    std::optional<double> Elevation(double lat_deg, double lon_deg);

    // Which elevation source Elevation() would use at (lat, lon):
    // kHomeTiedDem / kRawDem / kFlatAtHome / kUnknown.
    core::ElevationSource SourceFor(double lat_deg, double lon_deg);

    // Count of .hgt files present under dem_directory (self-test output).
    int DemTileCount() const;

private:
    struct Tile {
        int n = 0;  // 1201 or 3601
        std::vector<std::int16_t> heights;  // host-endian, row-major, row 0 = north edge
    };

    // Raw DEM sample (bilinear, metres as stored — the EGM96 surface), or
    // nullopt when no tile covers the point / the point is void.
    std::optional<double> SampleDem(double lat_deg, double lon_deg);
    const Tile* LoadTile(int lat_index, int lon_index);

    TerrainProviderConfig config_;
    bool has_home_tie_ = false;
    double home_ground_alt_m_ = 0.0;
    double offset_m_ = 0.0;

    // Small LRU: SRTM1 tiles are ~26 MB unpacked, so the cache is capped.
    static constexpr std::size_t kMaxCachedTiles = 4;
    std::map<std::pair<int, int>, std::shared_ptr<const Tile>> cache_;
    std::vector<std::pair<int, int>> lru_order_;
};

}  // namespace droppoint::hw
