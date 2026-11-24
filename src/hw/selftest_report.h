// hw/selftest_report.h
//
// Builds the plain-language self-test report for `droppoint --selftest`.
// Everything that needs no aircraft runs with --no-link (CI/install tests);
// the rest is reported as SKIP rather than invented.
//
// DJI-free by design: the PSDK entry point translates live telemetry /
// rangefinder / credentials state into the plain-data SelftestFacts struct
// below, so this builder runs and is unit-tested on any OS.
#pragma once

#include <string>
#include <vector>

#include "hal/i_telemetry.h"
#include "util/config.h"

namespace droppoint::hal {
class IRelease;
}

namespace droppoint::hw {

// Per-topic subscription outcome, mirroring psdk::PsdkTelemetry::TopicStatusEntry.
struct SelftestTopicStatus {
    std::string name;
    bool subscribed = false;
    int frequency_hz = 0;
    bool required = false;
};

struct SelftestFacts {
    const util::config::Config* config = nullptr;
    bool credentials_parsed = false;       // a file was found and parsed
    bool credentials_placeholder = false;  // parsed but still the shipped example values
    std::string credentials_path;          // searched path (empty = none found)
    bool credentials_parse_error = false;
    bool telemetry_present = false;        // link mode: telemetry adapter initialised
    std::vector<SelftestTopicStatus> topics;
    hal::GnssStatus gnss;
    bool rangefinder_initialised = false;  // link mode: camera manager up
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
std::string BuildSelftestReport(const SelftestFacts& in, bool* had_fail);

}  // namespace droppoint::hw
