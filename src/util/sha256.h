// util/sha256.h
//
// Compact SHA-256 (FIPS 180-4), used to hash the machine identity sources
// for the licence device code (hw/device_id.cpp). Written against the
// public specification — no external crypto dependency; Monocypher (the
// vendored Ed25519 code) carries BLAKE2b/SHA-512, not SHA-256.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace droppoint::util {

// One-shot SHA-256 over `data`. `Sha256Hex` is the lowercase hex form.
std::array<std::uint8_t, 32> Sha256(const std::string& data);
std::string Sha256Hex(const std::string& data);

}  // namespace droppoint::util
