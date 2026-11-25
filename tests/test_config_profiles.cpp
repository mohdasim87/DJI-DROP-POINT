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

TEST_CASE("Config: shipped profiles load with their drop kit") {
    REQUIRE(util::config::LoadAll(ConfigRoot() + "/profiles/m4t")
                .aircraft.release.kit == "czi_th2");
    REQUIRE(util::config::LoadAll(ConfigRoot() + "/profiles/m400")
                .aircraft.release.kit == "czi_th4");
}

TEST_CASE("Config: unknown release.kit rejected and lists the valid ids") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "badkit", "aircraft.yaml",
                                   "kit: \"czi_th2\"", "kit: \"no_such_kit\"");
    REQUIRE(LoadFailsWith(dir, "no_such_kit"));
    REQUIRE(LoadFailsWith(dir, "czi_ft4p"));  // a valid id appears in the message
}

TEST_CASE("Config: kit that does not fit the model is rejected, naming the ones that do") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "th4onm4t",
                                   "aircraft.yaml", "kit: \"czi_th2\"", "kit: \"czi_th4\"");
    REQUIRE(LoadFailsWith(dir, "does not fit"));
    REQUIRE(LoadFailsWith(dir, "jz_ad2"));  // the error names a kit that fits M4T

    const auto dir2 = MutateProfile(ConfigRoot() + "/profiles/m400", "ad2onm400",
                                    "aircraft.yaml", "kit: \"czi_th4\"", "kit: \"jz_ad2\"");
    REQUIRE(LoadFailsWith(dir2, "does not fit"));
}

TEST_CASE("Config: psdk_widget with jz_ad2 is rejected") {
    // jz_ad2 fits the M4T but lists only external_operator; psdk_widget is
    // additionally still an M400-only backend — either way it must not load.
    auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "pw_ad2", "aircraft.yaml",
                             "kit: \"czi_th2\"", "kit: \"jz_ad2\"");
    dir = MutateProfile(dir.string(), "pw_ad2b", "aircraft.yaml",
                        "backend: \"external_operator\"", "backend: \"psdk_widget\"");
    REQUIRE(LoadFailsWith(dir, "psdk_widget"));
}

TEST_CASE("Config: psdk_widget + czi_th4 on M350_RTK is rejected (backend is M400-only)") {
    // czi_th4 lists M350_RTK as compatible and allows psdk_widget, but the
    // Widget Manager backend itself remains M400-only.
    auto dir = MutateProfile(ConfigRoot() + "/profiles/m400", "m350pw", "aircraft.yaml",
                             "model: \"M400\"", "model: \"M350_RTK\"");
    dir = MutateProfile(dir.string(), "m350pwb", "aircraft.yaml",
                        "backend: \"external_operator\"", "backend: \"psdk_widget\"");
    REQUIRE(LoadFailsWith(dir, "psdk_widget"));
}

TEST_CASE("Config: pwm_servo with an operator-actuated kit is rejected") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "pwm_th2",
                                   "aircraft.yaml", "backend: \"external_operator\"",
                                   "backend: \"pwm_servo\"");
    REQUIRE(LoadFailsWith(dir, "pwm_servo"));
    REQUIRE(LoadFailsWith(dir, "czi_th2"));
}

TEST_CASE("Config: payload over the kit's per-hook rating is rejected and names both") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "hooklimit",
                                   "payloads.yaml", "mass_kg: 0.080", "mass_kg: 0.600");
    REQUIRE(LoadFailsWith(dir, "med_pouch_80g"));
    REQUIRE(LoadFailsWith(dir, "czi_th2"));
}

TEST_CASE("Config: empty release.description is filled from the kit") {
    const auto dir = MutateProfile(
        ConfigRoot() + "/profiles/m4t", "kitdesc", "aircraft.yaml",
        "description: \"CZI TH2 (triggered by the aircraft auxiliary bottom light, "
        "operated in DJI Pilot 2)\"",
        "description: \"\"");
    const auto cfg = util::config::LoadAll(dir.string());
    REQUIRE(cfg.aircraft.release.description.find("CZI") != std::string::npos);
    REQUIRE(cfg.aircraft.release.description.find("TH2") != std::string::npos);
}

TEST_CASE("Config: shipped profiles carry the expected auth_chip setting") {
    const auto m4t = util::config::LoadAll(ConfigRoot() + "/profiles/m4t");
    REQUIRE(m4t.aircraft.auth_chip.enabled);
    REQUIRE(m4t.aircraft.auth_chip.i2c_device == "/dev/i2c-1");
    REQUIRE(m4t.aircraft.auth_chip.reset_gpio_line == 4);
    const auto m400 = util::config::LoadAll(ConfigRoot() + "/profiles/m400");
    REQUIRE(!m400.aircraft.auth_chip.enabled);
}

TEST_CASE("Config: auth_chip enabled on manifold3 is rejected") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m400", "m400chip",
                                   "aircraft.yaml", "enabled: false", "enabled: true");
    REQUIRE(LoadFailsWith(dir, "auth_chip"));
    REQUIRE(LoadFailsWith(dir, "anifold"));
}

TEST_CASE("Config: auth_chip enabled with an empty i2c_device is rejected") {
    const auto dir = MutateProfile(ConfigRoot() + "/profiles/m4t", "emptyi2c",
                                   "aircraft.yaml", "i2c_device: \"/dev/i2c-1\"",
                                   "i2c_device: \"\"");
    REQUIRE(LoadFailsWith(dir, "i2c_device"));
}
