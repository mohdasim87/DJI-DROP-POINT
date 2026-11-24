// hw/terrain_provider.cpp
#include "terrain_provider.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace droppoint::hw {

namespace {

constexpr std::int16_t kHgtVoid = -32768;

// Parses an SRTM tile file name (N34E073.hgt / S05W120.hgt, any case) into
// the integer lat/lon of the tile's SW corner. Returns false on mismatch.
bool ParseTileName(const std::string& filename, int& lat_index, int& lon_index) {
    if (filename.size() != 11) return false;
    const char ns = static_cast<char>(std::toupper(filename[0]));
    const char ew = static_cast<char>(std::toupper(filename[3]));
    if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) return false;
    if (!std::isdigit(filename[1]) || !std::isdigit(filename[2]) ||
        !std::isdigit(filename[4]) || !std::isdigit(filename[5]) || !std::isdigit(filename[6])) {
        return false;
    }
    std::string ext = filename.substr(7);
    for (char& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext != ".hgt") return false;
    lat_index = std::stoi(filename.substr(1, 2));
    lon_index = std::stoi(filename.substr(4, 3));
    if (ns == 'S') lat_index = -lat_index;
    if (ew == 'W') lon_index = -lon_index;
    return true;
}

}  // namespace

TerrainProvider::TerrainProvider(TerrainProviderConfig config) : config_(std::move(config)) {}

int TerrainProvider::DemTileCount() const {
    namespace fs = std::filesystem;
    if (!fs::is_directory(config_.dem_directory)) return 0;
    int count = 0;
    for (const auto& entry : fs::directory_iterator(config_.dem_directory)) {
        int lat = 0, lon = 0;
        if (entry.is_regular_file() && ParseTileName(entry.path().filename().string(), lat, lon)) {
            ++count;
        }
    }
    return count;
}

void TerrainProvider::SetHomeTie(double lat_deg, double lon_deg, double ground_alt_ellipsoid_m) {
    has_home_tie_ = true;
    home_ground_alt_m_ = ground_alt_ellipsoid_m;
    const auto home_dem = SampleDem(lat_deg, lon_deg);
    offset_m_ = home_dem.has_value() ? ground_alt_ellipsoid_m - *home_dem : 0.0;
}

std::optional<double> TerrainProvider::Elevation(double lat_deg, double lon_deg) {
    const auto dem = SampleDem(lat_deg, lon_deg);
    if (dem.has_value()) {
        if (has_home_tie_) return *dem + offset_m_;
        if (config_.allow_raw_dem) return *dem;
        return std::nullopt;
    }
    if (has_home_tie_ && config_.allow_flat_terrain_fallback) {
        return home_ground_alt_m_;
    }
    return std::nullopt;
}

core::ElevationSource TerrainProvider::SourceFor(double lat_deg, double lon_deg) {
    const auto dem = SampleDem(lat_deg, lon_deg);
    if (dem.has_value()) {
        if (has_home_tie_) return core::ElevationSource::kHomeTiedDem;
        return config_.allow_raw_dem ? core::ElevationSource::kRawDem
                                     : core::ElevationSource::kUnknown;
    }
    if (has_home_tie_ && config_.allow_flat_terrain_fallback) {
        return core::ElevationSource::kFlatAtHome;
    }
    return core::ElevationSource::kUnknown;
}

std::optional<double> TerrainProvider::SampleDem(double lat_deg, double lon_deg) {
    if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg) || lat_deg < -90.0 ||
        lat_deg > 90.0 || lon_deg < -180.0 || lon_deg > 180.0) {
        return std::nullopt;
    }
    const int lat_index = static_cast<int>(std::floor(lat_deg));
    const int lon_index = static_cast<int>(std::floor(lon_deg));
    const Tile* tile = LoadTile(lat_index, lon_index);
    if (tile == nullptr) return std::nullopt;

    const int n = tile->n;
    const double fx_full = (lon_deg - lon_index) * (n - 1);
    // Row 0 is the tile's NORTH edge.
    const double fy_full = ((lat_index + 1) - lat_deg) * (n - 1);
    const int x0 = std::clamp(static_cast<int>(fx_full), 0, n - 2);
    const int y0 = std::clamp(static_cast<int>(fy_full), 0, n - 2);
    const double fx = std::clamp(fx_full - x0, 0.0, 1.0);
    const double fy = std::clamp(fy_full - y0, 0.0, 1.0);

    const auto at = [&](int row, int col) -> std::int16_t {
        return tile->heights[static_cast<std::size_t>(row) * n + col];
    };
    const std::int16_t h00 = at(y0, x0);
    const std::int16_t h10 = at(y0, x0 + 1);
    const std::int16_t h01 = at(y0 + 1, x0);
    const std::int16_t h11 = at(y0 + 1, x0 + 1);
    if (h00 == kHgtVoid || h10 == kHgtVoid || h01 == kHgtVoid || h11 == kHgtVoid) {
        return std::nullopt;
    }
    const double top = h00 * (1.0 - fx) + h10 * fx;
    const double bottom = h01 * (1.0 - fx) + h11 * fx;
    return top * (1.0 - fy) + bottom * fy;
}

const TerrainProvider::Tile* TerrainProvider::LoadTile(int lat_index, int lon_index) {
    const std::pair<int, int> key{lat_index, lon_index};
    if (const auto it = cache_.find(key); it != cache_.end()) {
        auto pos = std::find(lru_order_.begin(), lru_order_.end(), key);
        if (pos != lru_order_.end()) {
            lru_order_.erase(pos);
            lru_order_.push_back(key);
        }
        return it->second.get();
    }

    // Locate the tile file: both hemisphere sign conventions are generated
    // from the SW corner indices.
    const char ns = lat_index < 0 ? 'S' : 'N';
    const char ew = lon_index < 0 ? 'W' : 'E';
    char filename[16];
    std::snprintf(filename, sizeof(filename), "%c%02d%c%03d.hgt", ns,
                  std::abs(lat_index), ew, std::abs(lon_index));
    const std::filesystem::path path = std::filesystem::path(config_.dem_directory) / filename;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return nullptr;

    const std::streamoff size = in.tellg();
    const std::size_t bytes = static_cast<std::size_t>(std::max<std::streamoff>(size, 0));
    const std::size_t samples = bytes / 2;
    int n = 0;
    if (samples == 1201ull * 1201ull) {
        n = 1201;
    } else if (samples == 3601ull * 3601ull) {
        n = 3601;
    } else {
        return nullptr;
    }

    auto tile = std::make_shared<Tile>();
    tile->n = n;
    tile->heights.resize(samples);
    in.seekg(0);
    std::vector<std::uint8_t> raw(bytes);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytes));
    if (!in) return nullptr;
    for (std::size_t i = 0; i < samples; ++i) {
        // Big-endian signed 16-bit -> host int16.
        const auto v = static_cast<std::int16_t>((raw[2 * i] << 8) | raw[2 * i + 1]);
        tile->heights[i] = v;
    }

    if (cache_.size() >= kMaxCachedTiles && !lru_order_.empty()) {
        cache_.erase(lru_order_.front());
        lru_order_.erase(lru_order_.begin());
    }
    cache_[key] = tile;
    lru_order_.push_back(key);
    return tile.get();
}

}  // namespace droppoint::hw
