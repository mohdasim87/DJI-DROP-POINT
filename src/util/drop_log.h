// util/drop_log.h
//
// The drop log (spec section 11): "Every release records the full input
// state, release mode, computed release point, predicted impact,
// dispersion, profile used, gate states at firing, and a slot for the
// measured actual impact a human fills in later."
//
// Implemented as an append-only CSV. A SQLite mirror is a documented
// follow-on (see the note in drop_log.cpp) rather than vendored now, to
// avoid adding a second large dependency beyond yaml-cpp without a
// corresponding concrete need in this codebase yet; the CSV is a complete,
// diffable, awk/Excel-readable record on its own and is what
// tools/calibrate already consumes.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/gating.h"
#include "core/solution.h"

namespace droppoint::util {

struct DropLogEntry {
    std::uint64_t timestamp_us = 0;
    std::string release_mode;  // "manual" | "auto"
    std::string payload_name;
    core::EnuPoint release_point;
    core::EnuPoint predicted_impact;
    core::EnuPoint latched_target;
    double p50_dispersion_m = 0.0;
    double p90_dispersion_m = 0.0;
    std::string gate_states_summary;  // e.g. "all-clear" or "soft:RTK_FLOAT"
    // Filled in later by a human once the actual impact is measured
    // (spec 11); empty/NaN until then.
    std::optional<double> measured_dE_m;
    std::optional<double> measured_dN_m;
};

class DropLog {
public:
    // Opens (creating if needed) the CSV at `path`, writing a header row if
    // the file is new. Throws std::runtime_error if the directory is not
    // writable (spec 13: "startup writability check").
    explicit DropLog(const std::string& path);

    void Append(const DropLogEntry& entry);

private:
    std::string path_;
};

}  // namespace droppoint::util
