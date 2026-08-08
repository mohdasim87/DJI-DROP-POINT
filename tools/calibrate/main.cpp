// tools/calibrate/main.cpp
//
// Least-squares fit of CdA and mechanism delay from drops with measured
// impacts (spec section 11). Input: a CSV of drop records (one row per
// drop) with columns:
//   mass_kg,cda_nominal,release_agl_m,gs_mps,heading_deg,wind_mps,wind_from_deg,
//   temp_c,measured_dE_m,measured_dN_m,measured_fall_time_s
//
// Refuses on fewer than five drops. Reports residuals before and after a
// simple coordinate-descent search over a CdA scale factor and a mechanism
// delay offset, then prints the fitted payloads.yaml fields for a human to
// apply (this tool reports; it does not silently rewrite payloads.yaml,
// since a human should confirm scientific/engineering values before they
// become the flight configuration).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/atmosphere.h"
#include "core/ballistics.h"
#include "core/math_constants.h"
#include "core/wind.h"

using namespace droppoint::core;

namespace {

struct DropRecord {
    double mass_kg, cda_nominal, release_agl_m, gs_mps, heading_deg, wind_mps, wind_from_deg, temp_c;
    double measured_dE_m, measured_dN_m, measured_fall_time_s;
};

std::vector<DropRecord> LoadCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);

    std::vector<DropRecord> records;
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first) { first = false; continue; }  // skip header
        if (line.empty()) continue;
        std::istringstream ss(line);
        DropRecord r{};
        char comma;
        ss >> r.mass_kg >> comma >> r.cda_nominal >> comma >> r.release_agl_m >> comma >> r.gs_mps >> comma >>
            r.heading_deg >> comma >> r.wind_mps >> comma >> r.wind_from_deg >> comma >> r.temp_c >> comma >>
            r.measured_dE_m >> comma >> r.measured_dN_m >> comma >> r.measured_fall_time_s;
        records.push_back(r);
    }
    return records;
}

double ResidualForParams(const std::vector<DropRecord>& records, double cda_scale, double delay_s) {
    double sum_sq = 0.0;
    for (const auto& r : records) {
        ballistics::ForwardInputs in;
        in.mass_kg = r.mass_kg;
        const double cda = r.cda_nominal * cda_scale;
        in.cda_at_time = [cda](double) { return cda; };
        in.release_height_agl_m = r.release_agl_m;
        const double heading_rad = r.heading_deg * kPi / 180.0;
        in.initial_velocity = EnuVector{r.gs_mps * std::sin(heading_rad), r.gs_mps * std::cos(heading_rad), 0.0};
        const double wind_from_rad = r.wind_from_deg * kPi / 180.0;
        const double z_ref = r.release_agl_m;
        in.wind_at_height = [&](double h) {
            const double speed = wind::SpeedAtHeight(r.wind_mps, z_ref, h, 0.14);
            return wind::ToVector(speed, wind_from_rad);
        };
        in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(0.0), r.temp_c);

        const auto result = ballistics::IntegrateForward(in);
        // Account for mechanism delay: the effective release happens
        // `delay_s` later than commanded, which we approximate by
        // re-integrating from a release point advanced along the initial
        // velocity vector by delay_s (a first-order approximation adequate
        // for the small delays involved here).
        const double dE = result.impact_displacement.e + in.initial_velocity.e * delay_s - r.measured_dE_m;
        const double dN = result.impact_displacement.n + in.initial_velocity.n * delay_s - r.measured_dN_m;
        const double dT = result.fall_time_s - r.measured_fall_time_s;
        sum_sq += dE * dE + dN * dN + dT * dT * 4.0;  // weight fall-time error comparably to a few metres of miss
    }
    return std::sqrt(sum_sq / static_cast<double>(records.size()));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: droppoint_calibrate <drops.csv>\n");
        return 1;
    }

    std::vector<DropRecord> records;
    try {
        records = LoadCsv(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    if (records.size() < 5) {
        std::fprintf(stderr,
                      "refusing to calibrate: %zu drop(s) provided, at least 5 are required (spec section 11)\n",
                      records.size());
        return 1;
    }

    const double residual_before = ResidualForParams(records, 1.0, 0.0);

    // Coarse coordinate-descent grid search; adequate for the handful of
    // free parameters here and keeps this tool dependency-free.
    double best_scale = 1.0, best_delay = 0.0;
    double best_residual = residual_before;
    for (double scale = 0.5; scale <= 1.5; scale += 0.01) {
        for (double delay = -0.3; delay <= 0.3; delay += 0.01) {
            const double residual = ResidualForParams(records, scale, delay);
            if (residual < best_residual) {
                best_residual = residual;
                best_scale = scale;
                best_delay = delay;
            }
        }
    }

    std::printf("Calibration report (%zu drops)\n", records.size());
    std::printf("  Residual RMS before: %.3f\n", residual_before);
    std::printf("  Residual RMS after:  %.3f\n", best_residual);
    std::printf("  Fitted CdA scale factor: %.4f\n", best_scale);
    std::printf("  Fitted mechanism delay:  %.3f s\n", best_delay);
    std::printf(
        "\nApply these to payloads.yaml manually as:\n"
        "  cda_free: <nominal_cda> * %.4f\n"
        "  mechanism_delay_s: %.3f\n"
        "  calibrated: true\n"
        "  calibration_date: <today, ISO-8601>\n",
        best_scale, best_delay);
    return 0;
}
