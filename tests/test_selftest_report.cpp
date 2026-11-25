// tests/test_selftest_report.cpp
//
// The DJI-free self-test report builder: the no-link report must never
// crash on null/empty inputs, must mark link-only checks SKIP, must warn
// (not fail) on the shipped example credentials, and must pass on both
// shipped aircraft profiles the way a fresh .deb install would see them.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "test_framework.h"

#include "hw/selftest_report.h"
#include "util/config.h"

namespace {

using droppoint::hw::BuildSelftestReport;
using droppoint::hw::SelftestFacts;
namespace config = droppoint::util::config;

std::string ConfigRoot() { return DROPPOINT_CONFIG_DIR; }

// A fully-populated no-link facts struct for a given config: the shape a
// fresh install produces (real credentials absent or still the example).
SelftestFacts NoLinkFacts(const config::Config& cfg) {
    SelftestFacts f;
    f.config = &cfg;
    f.link = false;
    f.widget_dir = "./data/widget";
    f.widget_dir_ok = true;
    return f;
}

TEST_CASE("SelftestReport: null and empty inputs do not crash") {
    SelftestFacts f;  // everything null/zero
    bool had_fail = true;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(!r.empty());
    REQUIRE(had_fail);  // config == nullptr is a FAIL line
}

TEST_CASE("SelftestReport: no-link marks link-only checks SKIP") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    f.credentials_placeholder = true;
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("[SKIP]") != std::string::npos);
    REQUIRE(!had_fail);
}

TEST_CASE("SelftestReport: example placeholder credentials are WARN not FAIL") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    f.credentials_placeholder = true;
    f.credentials_path = "/etc/droppoint/credentials.yaml";
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("example placeholders") != std::string::npos);
    REQUIRE(!had_fail);

    // Real (non-placeholder) credentials parse -> OK line, still no fail.
    f.credentials_placeholder = false;
    const std::string r2 = BuildSelftestReport(f, &had_fail);
    REQUIRE(r2.find("[OK] credentials file parsed") != std::string::npos);
    REQUIRE(!had_fail);
}

TEST_CASE("SelftestReport: missing or broken credentials FAIL") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    bool had_fail = false;
    std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("no credentials file found") != std::string::npos);
    REQUIRE(had_fail);

    had_fail = false;
    f.credentials_parse_error = true;
    f.credentials_path = "/etc/droppoint/credentials.yaml";
    r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("could not be parsed") != std::string::npos);
    REQUIRE(had_fail);
}

TEST_CASE("SelftestReport: weight margin nets out onboard equipment") {
    // m4t: max 0.200 kg - onboard 0.110 kg = 0.090 kg releasable budget, so
    // marker_light_40g -> 0.050 and med_pouch_80g -> 0.010, not 0.160/0.120.
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(!had_fail);
    REQUIRE(r.find("'marker_light_40g' weight margin 0.050 kg") != std::string::npos);
    REQUIRE(r.find("'med_pouch_80g' weight margin 0.010 kg") != std::string::npos);
    REQUIRE(r.find("0.160") == std::string::npos);
}

TEST_CASE("SelftestReport: fresh-install shape passes on both profiles") {
    for (const char* profile : {"profiles/m4t", "profiles/m400"}) {
        const auto cfg = config::LoadAll(ConfigRoot() + "/" + profile);
        auto f = NoLinkFacts(cfg);
        f.credentials_parsed = true;
        f.credentials_placeholder = true;
        f.credentials_path = "/etc/droppoint/credentials.yaml";
        bool had_fail = false;
        const std::string r = BuildSelftestReport(f, &had_fail);
        REQUIRE(!had_fail);
        REQUIRE(r.find("[SKIP]") != std::string::npos);
    }
}

TEST_CASE("SelftestReport: widget dir failure is FAIL") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    f.widget_dir_ok = false;
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("widget files could not be generated") != std::string::npos);
    REQUIRE(had_fail);
}

}  // namespace

namespace {

// Minimal copy of the mutate-a-shipped-profile pattern from
// test_config_profiles.cpp (kept file-local like there).
std::filesystem::path MutateProfileForReport(const std::string& src_dir,
                                             const std::string& case_name,
                                             const std::string& from,
                                             const std::string& to) {
    const std::filesystem::path dst =
        std::filesystem::temp_directory_path() / ("droppoint_str_" + case_name);
    std::filesystem::remove_all(dst);
    std::filesystem::create_directories(dst);
    for (const char* f : {"aircraft.yaml", "payloads.yaml", "lenses.yaml", "system.yaml"}) {
        std::filesystem::copy_file(std::filesystem::path(src_dir) / f, dst / f,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    const std::filesystem::path target = dst / "aircraft.yaml";
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

}  // namespace

TEST_CASE("SelftestReport: drop kit INFO line for both shipped profiles") {
    {
        const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
        auto f = NoLinkFacts(cfg);
        f.credentials_parsed = true;
        bool had_fail = false;
        const std::string r = BuildSelftestReport(f, &had_fail);
        REQUIRE(!had_fail);
        REQUIRE(r.find("[INFO] drop kit: CZI TH2 Throwing Hook (2 hook(s), 0.070 kg, "
                       "connects to none (light sensor") != std::string::npos);
    }
    {
        const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m400");
        auto f = NoLinkFacts(cfg);
        f.credentials_parsed = true;
        bool had_fail = false;
        const std::string r = BuildSelftestReport(f, &had_fail);
        REQUIRE(!had_fail);
        REQUIRE(r.find("[INFO] drop kit: CZI TH4 V3 Throwing Hook Drop System "
                       "(4 hook(s), 0.320 kg") != std::string::npos);
    }
}

TEST_CASE("SelftestReport: no-kit warns for operator backends but not pwm_servo") {
    // root config: pwm_servo backend, no kit — nothing extra expected
    {
        const auto cfg = config::LoadAll(ConfigRoot());
        auto f = NoLinkFacts(cfg);
        f.credentials_parsed = true;
        bool had_fail = false;
        const std::string r = BuildSelftestReport(f, &had_fail);
        REQUIRE(r.find("no release.kit set") == std::string::npos);
        REQUIRE(r.find("drop kit:") == std::string::npos);
    }
    // m4t with the kit line removed: external_operator backend -> WARN
    {
        const auto dir = MutateProfileForReport(ConfigRoot() + "/profiles/m4t",
                                                "nokit", "  kit: \"czi_th2\"\n", "");
        const auto cfg = config::LoadAll(dir.string());
        auto f = NoLinkFacts(cfg);
        f.credentials_parsed = true;
        bool had_fail = false;
        const std::string r = BuildSelftestReport(f, &had_fail);
        REQUIRE(r.find("[WARN] no release.kit set") != std::string::npos);
        REQUIRE(!had_fail);
    }
}

TEST_CASE("SelftestReport: onboard equipment lighter than the kit warns") {
    // czi_th2 alone weighs 0.070 kg — claim only 0.010 of onboard equipment.
    const auto dir = MutateProfileForReport(ConfigRoot() + "/profiles/m4t",
                                            "lightequip", "onboard_equipment_kg: 0.110",
                                            "onboard_equipment_kg: 0.010");
    const auto cfg = config::LoadAll(dir.string());
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("onboard_equipment_kg is less than the drop kit alone weighs") !=
            std::string::npos);
    REQUIRE(!had_fail);
}

TEST_CASE("SelftestReport: auth chip INFO line, missing nodes WARN without link") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    f.credentials_placeholder = true;
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("[INFO] SDK certified chip: enabled on /dev/i2c-1, reset "
                   "/dev/gpiochip0 line 4") != std::string::npos);
    REQUIRE(r.find("[WARN] SDK certified chip device nodes not present: /dev/i2c-1 and "
                   "/dev/gpiochip0") != std::string::npos);
    REQUIRE(!had_fail);
}

TEST_CASE("SelftestReport: auth chip nodes present -> no WARN; with link missing is FAIL") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m4t");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    f.credentials_placeholder = true;
    f.auth_chip_i2c_node_present = true;
    f.auth_chip_gpio_node_present = true;
    bool had_fail = false;
    std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("SDK certified chip device nodes") == std::string::npos);

    f.auth_chip_i2c_node_present = false;
    f.link = true;
    r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("[FAIL] SDK certified chip device nodes missing") !=
            std::string::npos);
    REQUIRE(had_fail);
}

TEST_CASE("SelftestReport: chip not required for the m400 build") {
    const auto cfg = config::LoadAll(ConfigRoot() + "/profiles/m400");
    auto f = NoLinkFacts(cfg);
    f.credentials_parsed = true;
    f.credentials_placeholder = true;
    bool had_fail = false;
    const std::string r = BuildSelftestReport(f, &had_fail);
    REQUIRE(r.find("[INFO] SDK certified chip: not required for this build") !=
            std::string::npos);
}
