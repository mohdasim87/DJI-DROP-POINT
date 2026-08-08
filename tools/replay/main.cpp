// tools/replay/main.cpp
//
// Reruns a recorded flight log (spec section 11: "full telemetry at 10 Hz
// with all computed solutions, so tools/replay can rerun any flight through
// a modified solver offline") through the current core:: ballistics/gating
// implementation, so a solver change can be validated against real flight
// data before it ever runs on hardware.
//
// Input CSV columns (one row per 10 Hz sample):
//   t_s,aircraft_e,aircraft_n,aircraft_u,vel_e,vel_n,vel_u,target_e,target_n,target_u,
//   wind_speed_mps,wind_from_deg,temp_c
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/atmosphere.h"
#include "core/ballistics.h"
#include "core/geodesy.h"
#include "core/math_constants.h"
#include "core/wind.h"

using namespace droppoint::core;

namespace {
struct Row {
    double t_s, ae, an, au, ve, vn, vu, te, tn, tu, wind_speed, wind_from_deg, temp_c;
};

std::vector<Row> LoadCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
    std::vector<Row> rows;
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        std::istringstream ss(line);
        Row r{};
        char c;
        ss >> r.t_s >> c >> r.ae >> c >> r.an >> c >> r.au >> c >> r.ve >> c >> r.vn >> c >> r.vu >> c >> r.te >>
            c >> r.tn >> c >> r.tu >> c >> r.wind_speed >> c >> r.wind_from_deg >> c >> r.temp_c;
        rows.push_back(r);
    }
    return rows;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: droppoint_replay <flight_log.csv>\n");
        return 1;
    }

    std::vector<Row> rows;
    try {
        rows = LoadCsv(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("t_s,converged,release_along_track_m,fall_time_s,miss_m\n");
    for (const auto& r : rows) {
        ballistics::InverseInputs in;
        in.mass_kg = 1.5;  // replayed against the reference profile; pass --payload in a future revision
        in.cda_at_time = [](double) { return 0.03; };
        in.aircraft_position = EnuPoint{r.ae, r.an, r.au};
        in.aircraft_velocity = EnuVector{r.ve, r.vn, r.vu};
        in.target = EnuPoint{r.te, r.tn, r.tu};
        const double z_ref = r.au - r.tu;
        const double wind_from_rad = r.wind_from_deg * kPi / 180.0;
        in.wind_at_height = [&](double h) {
            const double speed = wind::SpeedAtHeight(r.wind_speed, z_ref, h, 0.14);
            return wind::ToVector(speed, wind_from_rad);
        };
        in.air_density_kg_m3 = atmosphere::DensityKgM3(atmosphere::PressurePa(r.au), r.temp_c);

        const auto result = ballistics::SolveInverse(in);
        double miss = -1.0;
        if (result.converged) {
            const EnuPoint impact{result.release_point.e + result.ballistics_at_release.impact_displacement.e,
                                   result.release_point.n + result.ballistics_at_release.impact_displacement.n,
                                   in.target.u};
            miss = geodesy::HorizontalRange(impact, in.target);
        }
        std::printf("%.2f,%d,%.2f,%.2f,%.2f\n", r.t_s, result.converged, result.release_along_track_distance_m,
                    result.ballistics_at_release.fall_time_s, miss);
    }
    return 0;
}
