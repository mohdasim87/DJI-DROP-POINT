// psdk/selftest.h
//
// Status: unverified — requires hardware.
//
// Builds the plain-language self-test report for `droppoint --selftest`.
// Everything that needs no aircraft runs with --no-link (CI/install tests);
// the rest is reported as SKIPPED rather than invented. With a live link the
// caller additionally fills the PsdkTelemetry / rangefinder / release
// pointers after init and the same builder reports real results.
#pragma once

#include <optional>
#include <string>

#include "util/config.h"

namespace droppoint::hal {
class IRelease;
}

namespace droppoint::psdk {

class PsdkTelemetry;
class PsdkRangefinder;
class Credentials;

struct SelftestInput {
    const util::config::Config* config = nullptr;
    const Credentials* credentials = nullptr;      // set when a file was found and parsed
    std::string credentials_path;                  // searched path (empty = none found)
    bool credentials_parse_error = false;
    const PsdkTelemetry* telemetry = nullptr;      // set only in link mode after Init()
    const PsdkRangefinder* rangefinder = nullptr;  // set when camera manager inited
    hal::IRelease* release = nullptr;
    int dem_tile_count = 0;
    bool home_tie = false;
    double home_tie_alt_m = 0.0;
    bool link = false;
    bool model_matches = false;
    std::string aircraft_reported;  // model string from the FC, link mode only
    std::string widget_dir;         // generated widget dir (checked on disk)
    bool widget_dir_ok = false;
};

// One [OK]/[WARN]/[FAIL]/[SKIP]/[INFO] line per check, in a stable order.
// Returns the full report text and sets *had_fail on any FAIL line.
std::string BuildSelftestReport(const SelftestInput& in, bool* had_fail);

}  // namespace droppoint::psdk
