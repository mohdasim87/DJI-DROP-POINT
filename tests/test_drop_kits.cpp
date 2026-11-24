// tests/test_drop_kits.cpp
//
// The drop-kit catalogue (util/drop_kits): structural invariants, the
// KitsForModel lookup for the two shipped aircraft, and a doc-sync check
// that keeps src/util/drop_kits.cpp and docs/DROP-KITS.md agreeing on the
// `release.kit` ids.
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_framework.h"
#include "util/config.h"
#include "util/drop_kits.h"

using namespace droppoint;

namespace {

std::vector<std::string> KitIdsForModel(const std::string& model) {
    std::vector<std::string> ids;
    for (const auto* k : util::KitsForModel(model)) ids.push_back(k->id);
    return ids;
}

std::string ReadRepoFile(const std::string& rel) {
    std::ifstream in(std::string(DROPPOINT_REPO_ROOT) + "/" + rel);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("DropKits: ids are unique and non-empty") {
    std::vector<std::string> ids;
    for (const auto& k : util::DropKitCatalogue()) {
        REQUIRE(!k.id.empty());
        REQUIRE(std::find(ids.begin(), ids.end(), k.id) == ids.end());
        ids.push_back(k.id);
        REQUIRE(util::FindDropKit(k.id) == &k);
    }
    REQUIRE(util::FindDropKit("no_such_kit") == nullptr);
}

TEST_CASE("DropKits: every aircraft model is known, every kit has a backend") {
    const auto& known = util::config::KnownAircraftModels();
    for (const auto& k : util::DropKitCatalogue()) {
        REQUIRE(!k.aircraft_models.empty());
        for (const auto& m : k.aircraft_models) {
            REQUIRE(std::find(known.begin(), known.end(), m) != known.end());
        }
        REQUIRE(!k.backends.empty());
    }
}

TEST_CASE("DropKits: KitsForModel returns exactly the expected kits") {
    const std::vector<std::string> m4t = {"czi_th2", "czi_ft4p", "jz_ad2",
                                          "custom_pwm_servo"};
    const std::vector<std::string> m400 = {"czi_th4", "jz_pts4", "jz_ut4a", "jz_pl10",
                                           "dromight_talon", "cybowindow_4hook",
                                           "custom_pwm_servo"};
    REQUIRE(KitIdsForModel("M4T") == m4t);
    REQUIRE(KitIdsForModel("M400") == m400);
    REQUIRE(util::KitsForModel("M30").size() == 1);  // only the DIY servo
}

TEST_CASE("DropKits: FormatKitTable lists every kit and marks the fit") {
    const std::string plain = util::FormatKitTable();
    for (const auto& k : util::DropKitCatalogue()) {
        REQUIRE(plain.find(k.id) != std::string::npos);
    }
    const std::string marked = util::FormatKitTable("M4T");
    REQUIRE(marked.find("'M4T'") != std::string::npos);
    // the '*' marker column: lines for fitting kits start with '* '
    std::istringstream lines(marked);
    std::string line;
    int starred = 0;
    while (std::getline(lines, line)) {
        if (line.rfind("* ", 0) == 0) ++starred;
    }
    REQUIRE(starred == 4);
}

TEST_CASE("DropKits: catalogue ids stay in sync with docs/DROP-KITS.md") {
    const std::string doc = ReadRepoFile("docs/DROP-KITS.md");
    // forward: every catalogue id appears in the doc as `id`
    for (const auto& k : util::DropKitCatalogue()) {
        REQUIRE(doc.find("`" + k.id + "`") != std::string::npos);
    }
    // reverse: every kit id quoted in the doc's tables (a backticked token
    // in the last column, i.e. `id` followed by the row's closing pipe)
    // exists in the catalogue
    std::istringstream lines(doc);
    std::string line;
    int found = 0;
    while (std::getline(lines, line)) {
        const auto tick = line.rfind('`');
        const auto open = line.rfind('`', tick == 0 ? 0 : tick - 1);
        if (tick == std::string::npos || open == std::string::npos || open == tick) {
            continue;
        }
        const std::string token = line.substr(open + 1, tick - open - 1);
        const std::string tail = line.substr(tick + 1);
        if (tail.find_first_not_of(" |") != std::string::npos) continue;  // not last cell
        if (token.empty() ||
            token.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") !=
                std::string::npos) {
            continue;  // e.g. `release.kit`, `droppoint --list-kits`
        }
        REQUIRE(util::FindDropKit(token) != nullptr);
        ++found;
    }
    // 11 rows: custom_pwm_servo appears in both aircraft tables
    REQUIRE(found >= static_cast<int>(util::DropKitCatalogue().size()));
}
