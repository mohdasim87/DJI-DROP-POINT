// tests/test_selftest_report.cpp
//
// The DJI-free self-test report builder: the no-link report must never
// crash on null/empty inputs, must mark link-only checks SKIP, must warn
// (not fail) on the shipped example credentials, and must pass on both
// shipped aircraft profiles the way a fresh .deb install would see them.
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
