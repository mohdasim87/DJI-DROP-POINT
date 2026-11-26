// hw/device_id.h
//
// Stable per-machine device identity for licence binding. Plain file
// reads only — no dbus, no libudev — so the binary stays statically
// linkable for the .deb (glibc-only runtime deps).
//
// Hash source, FIRST available under `root`:
//   a) sys/firmware/devicetree/base/serial-number   (Raspberry Pi)
//   b) etc/machine-id                              (generic Linux; Manifold
//   c) var/lib/dbus/machine-id                      3 container fallback)
//   d) sys/class/net/<iface>/address               (first non-loopback MAC)
// NUL bytes are stripped from whatever is read.
//
// `root` is "" in production (real absolute paths) and a temp dir in
// tests — every candidate path is `root + "/sys/..."` etc. so nothing
// hardware-specific leaks into the test build.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace droppoint::hw {

// Ordered candidate file paths under `root` (MAC addresses are expanded
// from sys/class/net/* in order). Exposed so tests and diagnostics can
// report exactly what was tried.
std::vector<std::string> DeviceIdCandidatePaths(const std::string& root);

// SHA-256 over the first non-empty source's bytes (NULs stripped).
// nullopt when no source yields anything.
std::optional<std::array<std::uint8_t, 32>> ComputeDeviceHash(const std::string& root);

// Display form: "DJI-XXXXX-XXXXX-XXXXX", or "DJI-UNAVAILABLE" when no
// identity source exists (logs which candidates were tried).
std::string DeviceCode(const std::string& root);

// The root CheckLicense etc. use in production: honours DP_TEST_ROOT
// (install-test hook) so the real /sys and /etc are never touched there.
std::string DeviceIdRoot();

}  // namespace droppoint::hw
