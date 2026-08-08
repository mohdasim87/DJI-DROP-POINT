// A SQLite mirror (spec 11: "Drop log — SQLite plus CSV mirror") is a
// documented follow-on: vendor sqlite3's single-file amalgamation under
// third_party/sqlite3/ and add a second writer here that inserts the same
// fields into a `drops` table. The CSV format below is the schema that
// writer would mirror, so adding it later does not change this file's
// public interface.
#include "drop_log.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace droppoint::util {

namespace {
constexpr const char* kHeader =
    "timestamp_us,release_mode,payload_name,"
    "release_e,release_n,release_u,"
    "impact_e,impact_n,impact_u,"
    "target_e,target_n,target_u,"
    "p50_dispersion_m,p90_dispersion_m,gate_states,"
    "measured_dE_m,measured_dN_m\n";
}

DropLog::DropLog(const std::string& path) : path_(path) {
    namespace fs = std::filesystem;
    const bool exists = fs::exists(path_);
    if (!exists) {
        const fs::path parent = fs::path(path_).parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
        }
    }

    std::ofstream test(path_, std::ios::app);
    if (!test) {
        throw std::runtime_error("drop log path is not writable: " + path_);
    }
    if (!exists) {
        test << kHeader;
    }
}

void DropLog::Append(const DropLogEntry& e) {
    std::ofstream f(path_, std::ios::app);
    if (!f) throw std::runtime_error("drop log path became unwritable: " + path_);

    f << e.timestamp_us << ',' << e.release_mode << ',' << e.payload_name << ','
      << e.release_point.e << ',' << e.release_point.n << ',' << e.release_point.u << ','
      << e.predicted_impact.e << ',' << e.predicted_impact.n << ',' << e.predicted_impact.u << ','
      << e.latched_target.e << ',' << e.latched_target.n << ',' << e.latched_target.u << ','
      << e.p50_dispersion_m << ',' << e.p90_dispersion_m << ',' << e.gate_states_summary << ',';
    if (e.measured_dE_m.has_value()) f << *e.measured_dE_m; 
    f << ',';
    if (e.measured_dN_m.has_value()) f << *e.measured_dN_m;
    f << '\n';
}

}  // namespace droppoint::util
