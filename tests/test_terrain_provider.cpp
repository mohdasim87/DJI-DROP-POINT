// tests/test_terrain_provider.cpp
//
// SRTM .hgt reading and the elevation-source policy: bilinear
// interpolation, void handling, missing tiles, the home-tie offset, raw-DEM
// refusal, flat fallback and SourceFor() reporting.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "test_framework.h"

#include "hw/terrain_provider.h"

namespace {

using droppoint::core::ElevationSource;
using droppoint::hw::TerrainProvider;
using droppoint::hw::TerrainProviderConfig;

std::filesystem::path g_dir;

// Writes a synthetic 1201x1201 SRTM3 tile for the SW corner (lat, lon) with
// height = lat_deg_fraction-independent linear ramp: h = base + row_south ?
// Use a known gradient so bilinear results are predictable:
//   h(row, col) = base + col  (metres, varies 1 m per step west->east)
void WriteTile(const std::filesystem::path& dir, int lat, int lon, std::int16_t base,
               bool with_void = false) {
    const int n = 1201;
    char name[16];
    std::snprintf(name, sizeof(name), "%c%02d%c%03d.hgt", lat < 0 ? 'S' : 'N',
                  std::abs(lat), lon < 0 ? 'W' : 'E', std::abs(lon));
    std::vector<std::uint8_t> bytes(2ull * n * n);
    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) {
            std::int16_t h = static_cast<std::int16_t>(base + col);
            if (with_void && row == n / 2 && col == n / 2) h = -32768;
            const std::size_t i = 2ull * (row * n + col);
            bytes[i] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
            bytes[i + 1] = static_cast<std::uint8_t>(h & 0xFF);
        }
    }
    std::ofstream out(dir / name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

TerrainProviderConfig Cfg(bool raw = false, bool flat = true) {
    TerrainProviderConfig c;
    c.dem_directory = g_dir.string();
    c.allow_raw_dem = raw;
    c.allow_flat_terrain_fallback = flat;
    return c;
}

}  // namespace

TEST_CASE("Terrain: bilinear interpolation inside a tile") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_interp";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);
    WriteTile(g_dir, 34, 73, 500);

    TerrainProvider tp(Cfg(/*raw=*/true));
    // h(row,col) = 500 + col; col = dlon * 1200. At lon 73.25 -> col 300.
    const auto e = tp.Elevation(34.5, 73.25);
    REQUIRE(e.has_value());
    REQUIRE_NEAR(*e, 800.0, 1.0);
    // Same height at different latitude within the tile.
    REQUIRE_NEAR(*tp.Elevation(34.1, 73.25), 800.0, 1.0);
    REQUIRE(tp.SourceFor(34.5, 73.25) == ElevationSource::kRawDem);
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: void sample returns nullopt") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_void";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);
    WriteTile(g_dir, 34, 73, 500, /*with_void=*/true);

    TerrainProvider tp(Cfg(/*raw=*/true));
    // The void sits at row 600, col 600 -> lat ~34.5, lon ~73.5.
    REQUIRE(!tp.Elevation(34.5, 73.5).has_value());
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: missing tile returns nullopt and kUnknown") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_missing";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);

    TerrainProvider tp(Cfg(/*raw=*/true, /*flat=*/false));
    REQUIRE(!tp.Elevation(10.0, 10.0).has_value());
    REQUIRE(tp.SourceFor(10.0, 10.0) == ElevationSource::kUnknown);
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: home tie offsets the DEM to the measured altitude") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_tie";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);
    WriteTile(g_dir, 34, 73, 500);  // h = 500 + col

    TerrainProvider tp(Cfg());  // raw off
    // Without a tie and without allow_raw_dem, elevation is refused.
    REQUIRE(!tp.Elevation(34.5, 73.25).has_value());
    REQUIRE(tp.SourceFor(34.5, 73.25) == ElevationSource::kUnknown);

    // Aircraft measured 820 m at the home point where raw DEM says 800:
    // offset = +20 applied everywhere.
    tp.SetHomeTie(34.5, 73.25, 820.0);
    REQUIRE(tp.HasHomeTie());
    const auto e = tp.Elevation(34.5, 73.25);
    REQUIRE(e.has_value());
    REQUIRE_NEAR(*e, 820.0, 1.5);
    REQUIRE(tp.SourceFor(34.5, 73.25) == ElevationSource::kHomeTiedDem);
    // Offset applied elsewhere in the tile too (col 400 -> 900 + 20).
    const auto e2 = tp.Elevation(34.5, 73.0 + 400.0 / 1200.0);
    REQUIRE(e2.has_value());
    REQUIRE_NEAR(*e2, 920.0, 1.5);
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: raw DEM still refused after tie when tile is missing") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_flat";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);  // no tiles at all

    TerrainProvider tp(Cfg(/*raw=*/false, /*flat=*/true));
    tp.SetHomeTie(10.0, 10.0, 55.0);
    const auto e = tp.Elevation(10.01, 10.01);
    REQUIRE(e.has_value());
    REQUIRE_NEAR(*e, 55.0, 1e-9);
    REQUIRE(tp.SourceFor(10.01, 10.01) == ElevationSource::kFlatAtHome);
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: flat fallback disabled returns nullopt") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_noflat";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);

    TerrainProvider tp(Cfg(/*raw=*/false, /*flat=*/false));
    tp.SetHomeTie(10.0, 10.0, 55.0);
    REQUIRE(!tp.Elevation(10.01, 10.01).has_value());
    REQUIRE(tp.SourceFor(10.01, 10.01) == ElevationSource::kUnknown);
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: DemTileCount counts only valid .hgt names") {
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_count";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);
    WriteTile(g_dir, 34, 73, 100);
    WriteTile(g_dir, 35, 73, 100);
    {  // decoys that must not be counted
        std::ofstream(g_dir / "readme.txt") << "x";
        std::ofstream(g_dir / "N34E073.hgt.tmp") << "x";
    }
    TerrainProvider tp(Cfg());
    REQUIRE(tp.DemTileCount() == 2);
    std::filesystem::remove_all(g_dir);
}

TEST_CASE("Terrain: non-existent dem_directory yields no tiles, no throw") {
    // The install-test runner has no /opt/droppoint/data/dem — the provider
    // must degrade quietly rather than throw out of DemTileCount()/Elevation().
    g_dir = std::filesystem::temp_directory_path() / "dp_dem_absent_9f3d";
    std::filesystem::remove_all(g_dir);  // ensure it does not exist

    TerrainProvider tp(Cfg(/*raw=*/true));
    REQUIRE(tp.DemTileCount() == 0);
    REQUIRE(!tp.Elevation(34.5, 73.25).has_value());
    REQUIRE(tp.SourceFor(34.5, 73.25) == ElevationSource::kUnknown);

    // Home tie on a dir with no tiles: offset 0, flat fallback serves home alt.
    tp.SetHomeTie(34.5, 73.25, 412.0);
    const auto e = tp.Elevation(34.5, 73.25);
    REQUIRE(e.has_value());
    REQUIRE_NEAR(*e, 412.0, 1e-6);
    REQUIRE(tp.SourceFor(34.5, 73.25) == ElevationSource::kFlatAtHome);
}
