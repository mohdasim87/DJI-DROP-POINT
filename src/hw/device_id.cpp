// hw/device_id.cpp — see header.
#include "device_id.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "util/license.h"
#include "util/logging.h"
#include "util/sha256.h"

namespace droppoint::hw {

namespace {

// File-based identity sources in priority order; MAC addresses are
// appended dynamically by DeviceIdCandidatePaths.
const char* kFileSources[] = {
    "/sys/firmware/devicetree/base/serial-number",  // Raspberry Pi
    "/etc/machine-id",                              // generic Linux / Manifold 3
    "/var/lib/dbus/machine-id",                     // dbus-only fallback
};

std::string ReadSource(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string data = ss.str();
    std::string clean;
    clean.reserve(data.size());
    for (const char c : data) {
        if (c != '\0') clean.push_back(c);  // devicetree serial is NUL-terminated
    }
    // Trim whitespace/newlines.
    const auto first = clean.find_first_not_of(" \t\r\n");
    const auto last = clean.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return clean.substr(first, last - first + 1);
}

}  // namespace

std::vector<std::string> DeviceIdCandidatePaths(const std::string& root) {
    std::vector<std::string> out;
    for (const char* s : kFileSources) {
        out.push_back(root + s);
    }
    // First non-loopback MAC, sorted for determinism.
    std::error_code ec;
    const std::filesystem::path net_root =
        root.empty() ? "/sys/class/net" : std::filesystem::path(root) / "sys/class/net";
    std::vector<std::filesystem::path> ifaces;
    for (const auto& e : std::filesystem::directory_iterator(net_root, ec)) {
        ifaces.push_back(e.path());
    }
    std::sort(ifaces.begin(), ifaces.end());
    for (const auto& i : ifaces) {
        if (i.filename() == "lo") continue;
        out.push_back((i / "address").string());
    }
    return out;
}

std::optional<std::array<std::uint8_t, 32>> ComputeDeviceHash(const std::string& root) {
    const auto candidates = DeviceIdCandidatePaths(root);
    for (const auto& path : candidates) {
        const std::string data = ReadSource(path);
        if (data.empty()) continue;
        DP_LOG_INFO("device_id", "device identity source: " + path);
        return util::Sha256(data);
    }
    std::string tried;
    for (const auto& c : candidates) tried += " " + c;
    DP_LOG_WARN("device_id", "no device identity source found; tried:" + tried);
    return std::nullopt;
}

std::string DeviceCode(const std::string& root) {
    const auto hash = ComputeDeviceHash(root);
    if (!hash.has_value()) return "DJI-UNAVAILABLE";
    return util::license::DeviceCodeFromHash(*hash);
}

std::string DeviceIdRoot() {
    const char* r = std::getenv("DP_TEST_ROOT");
    return (r != nullptr && *r != '\0') ? r : "";
}

}  // namespace droppoint::hw
