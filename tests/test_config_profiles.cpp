// tests/test_config_profiles.cpp
//
// Loads the shipped config directories (root + aircraft profiles) and
// verifies the cross-validation rules in util::config::LoadAll: payload
// weight vs max_payload_kg, the aircraft-model allowlist, and the
// auto-default vs operator-actuated-backend conflict.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "test_framework.h"
#include "util/config.h"

using namespace droppoint;

namespace {

namespace fs = std::filesystem;

std::string ConfigRoot() { return DROPPOINT_CONFIG_DIR; }

// Copies a shipped profile dir to a temp location and applies a text
// replacement inside one file, so negative cases mutate a known-good
// config rather than depending on hand-written fixtures.
fs::path MutateProfile(const std::string& src_dir, const std::string& case_name,
                       const std::string& file_name, const std::string& from,
                       const std::string& to) {
    const fs::path dst = fs::temp_directory_path() / ("droppoint_cfg_" + case_name);
    fs::remove_all(dst);
    fs::create_directories(dst);
    for (const char* f : {"aircraft.yaml", "payloads.yaml", "lenses.yaml", "system.yaml"}) {
        fs::copy_file(fs::path(src_dir) / f, dst / f, fs::copy_options::overwrite_existing);
    }

    const fs::path target = dst / file_name;
    std::ifstream in(target);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    in.close();
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    std::ofstream out(target, std::ios::trunc);
    out << text;
    return dst;
}

bool LoadFailsWith(const fs::path& dir, const std::string& needle) {
    try {
        util::config::LoadAll(dir.string());
    } catch (const util::config::ConfigError& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

}  // namespace

TEST_CASE("Config: root config dir loads") {
    const auto cfg = util::config::LoadAll(ConfigRoot());
    REQUIRE(cfg.aircraft.max_payload_kg > 0.0);
    REQUIRE(!cfg.payloads.empty());
    for (const auto& p : cfg.payloads) {
        REQUIRE(p.mass_kg + cfg.aircraft.onboard_equipment_kg <= cfg.aircraft.max_payload_kg);
    }
}

TEST_CASE("Config: profiles/m4t loads") {
    const auto cfg = util::config::LoadAll(ConfigRoot() + "/profiles/m4t");
    REQUIRE(cfg.aircraft.model == "M4T");
    REQUIRE(cfg.aircraft.release.backend == util::config::ReleaseBackend::kExternalOperator);
    REQUIRE(!cfg.payloads.empty());
}

TEST_CASE("Config: profiles/m400 loads") {
    const auto cfg = util::config::LoadAll(ConfigRoot() + "/profiles/m400");
    REQUIRE(cfg.aircraft.model == "M400");
    REQUIRE(cfg.aircraft.release.backend == util::config::ReleaseBackend::kExternalOperator);
    REQUIRE(!cfg.payloads.empty());
}

TEST_CASE("Config: overweight payload rejected and names the payload") {
    // m4t budget: max 0.200 - equipment 0.110 = 0.090 kg; 0.150 exceeds it.
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "overweight", "payloads.yaml",
                                   "mass_kg: 0.080", "mass_kg: 0.150");
    REQUIRE(LoadFailsWith(dir, "med_pouch_80g"));
}

TEST_CASE("Config: unknown aircraft model rejected") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "badmodel", "aircraft.yaml",
                                   "model: \"M4T\"", "model: \"M999\"");
    REQUIRE(LoadFailsWith(dir, "M999"));
}

TEST_CASE("Config: auto default + external_operator backend rejected") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "auto_external", "system.yaml",
                                   "default_release_mode: \"manual\"",
                                   "default_release_mode: \"auto\"");
    REQUIRE(LoadFailsWith(dir, "external_operator"));
}

TEST_CASE("Config: payload without a positive target radius rejected") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "badradius", "payloads.yaml",
                                   "target_radius_m: 3.0", "target_radius_m: 0.0");
    REQUIRE(LoadFailsWith(dir, "target_radius_m"));
}
