#include <optional>

#include "terrain.h"
#include "test_framework.h"

using namespace droppoint::core;

// Regression vector 11: DEM datum bias. Applying a known bias shifts the
// intersection by the expected amount.
TEST_CASE("Regression 11: DEM datum bias shifts elevation by the applied amount") {
    terrain::DemSampleFn dem = [](double, double) -> std::optional<double> { return 100.0; };

    terrain::DatumBias bias;
    const double p_lrf_z = 107.5;  // measured true elevation via LRF
    const double dem_at_fix = 100.0;
    bias.Update(p_lrf_z, dem_at_fix, /*timestamp_us=*/1'000'000, EnuPoint{0, 0, 0});

    REQUIRE(bias.IsValid(1'000'000, EnuPoint{0, 0, 0}));
    REQUIRE_NEAR(bias.BiasMetres(), 7.5, 1e-9);

    const terrain::ElevationResult result =
        terrain::QueryElevation(10.0, 20.0, dem, bias, 1'000'000, EnuPoint{0, 0, 0});
    REQUIRE(result.available);
    REQUIRE(result.source == ElevationSource::kLrfCorrected);
    REQUIRE_NEAR(result.elevation_m, 107.5, 1e-9);
}

TEST_CASE("Terrain: bias expires after 120 seconds") {
    terrain::DemSampleFn dem = [](double, double) -> std::optional<double> { return 50.0; };
    terrain::DatumBias bias;
    bias.Update(55.0, 50.0, 0, EnuPoint{0, 0, 0});

    REQUIRE(bias.IsValid(119'000'000, EnuPoint{0, 0, 0}));
    REQUIRE(!bias.IsValid(121'000'000, EnuPoint{0, 0, 0}));

    const terrain::ElevationResult result =
        terrain::QueryElevation(0, 0, dem, bias, 121'000'000, EnuPoint{0, 0, 0});
    REQUIRE(result.source == ElevationSource::kRawDem);
    REQUIRE_NEAR(result.elevation_m, 50.0, 1e-9);
}

TEST_CASE("Terrain: bias expires after 200m of aircraft movement") {
    terrain::DemSampleFn dem = [](double, double) -> std::optional<double> { return 50.0; };
    terrain::DatumBias bias;
    bias.Update(55.0, 50.0, 0, EnuPoint{0, 0, 0});

    REQUIRE(bias.IsValid(0, EnuPoint{150, 0, 0}));
    REQUIRE(!bias.IsValid(0, EnuPoint{250, 0, 0}));
}

TEST_CASE("Terrain: missing DEM tile reports unavailable") {
    terrain::DemSampleFn dem = [](double, double) -> std::optional<double> { return std::nullopt; };
    terrain::DatumBias bias;
    const terrain::ElevationResult result = terrain::QueryElevation(0, 0, dem, bias, 0, EnuPoint{0, 0, 0});
    REQUIRE(!result.available);
}
