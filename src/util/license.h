// util/license.h
//
// DropPoint license-string codec (DJI-free, no filesystem): the `DPL1-`
// text format, the `DJI-XXXXX-XXXXX-XXXXX` device-code form, and Ed25519
// sign/verify helpers shared by the shipped binary (verify only) and the
// vendor-side droppoint-license tool (keygen + sign).
//
// Binary payload (77 bytes before base32):
//   byte 0        version = 1
//   bytes 1..8    first 8 bytes of the device hash (hw/device_id.cpp)
//   bytes 9..12   expiry_day, u32 little-endian, days since 2026-01-01
//   bytes 13..76  Ed25519 signature over the preceding 13 bytes
// Displayed as "DPL1-" + base32 (RFC 4648 alphabet, no padding) of the
// payload.
//
// Device code: "DJI-XXXXX-XXXXX-XXXXX" = base32 of the FIRST 9 bytes of
// the device hash, grouped 5-5-5 (15 chars, no padding). The license only
// binds the first 8 bytes — the 9th display byte is a check digit for the
// customer, and DeviceCodeToHash returns the bound 8-byte prefix.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace droppoint::util::license {

inline constexpr std::uint8_t kLicenseVersion = 1;
inline constexpr std::size_t kDeviceHashBoundBytes = 8;   // bytes a license binds
inline constexpr std::size_t kDeviceCodeBytes = 9;        // bytes the DJI- code shows
inline constexpr std::size_t kHeaderBytes = 13;           // version|device|expiry, signed
inline constexpr std::size_t kPayloadBytes = 77;          // signed payload + signature
// Days since the license epoch 2026-01-01 00:00 UTC.
inline constexpr std::int64_t kEpochDaysOffset = 20454;   // 2026-01-01 in Unix days

struct Payload {
    std::uint8_t version = kLicenseVersion;
    std::array<std::uint8_t, kDeviceHashBoundBytes> device_hash{};
    std::uint32_t expiry_day = 0;                          // days since 2026-01-01
    std::array<std::uint8_t, 64> signature{};
};

enum class VerifyResult {
    kValid,
    kExpired,
    kWrongDevice,
    kBadSignature,
    kMalformed,   // bad base32/length/version — also covers a zero pubkey
};

// RFC 4648 base32, uppercase, no padding. Decode is case-insensitive and
// returns nullopt on a bad character or leftover-bit waste.
std::string Base32Encode(const std::uint8_t* data, std::size_t len);
std::optional<std::string> Base32Decode(const std::string& text);  // binary in a string

// DJI-XXXXX-XXXXX-XXXXX <-> the bound 8-byte hash prefix.
std::string DeviceCodeFromHash(const std::array<std::uint8_t, 32>& device_hash);
std::optional<std::array<std::uint8_t, kDeviceHashBoundBytes>> DeviceCodeToHash(
    const std::string& code);

// DPL1-... <-> Payload. Parse rejects wrong length, wrong prefix and any
// version it does not know.
std::optional<Payload> ParseLicense(const std::string& text);
std::string FormatLicense(const Payload& payload);

// Sign the 12-byte header fields with a 64-byte Ed25519 secret key
// (Monocypher crypto_eddsa_key_pair output) and return the "DPL1-..." text.
// Vendor tool only — the shipped binary never links this into a path that
// sees the private key.
std::string BuildLicenseString(const std::array<std::uint8_t, 64>& secret_key,
                               const std::array<std::uint8_t, kDeviceHashBoundBytes>& device_hash,
                               std::uint32_t expiry_day);

// `public_key_hex` is the 64-char lowercase hex key embedded at build time
// (generated/util/license_pubkey.h). An all-zero or malformed key fails
// closed: every result is kMalformed.
// `device_hash` is the full 32-byte SHA-256 device hash — only the first
// 8 bytes are compared.
VerifyResult VerifyLicense(const Payload& payload, const std::string& public_key_hex,
                           const std::array<std::uint8_t, 32>& device_hash,
                           std::int64_t today_days);

// Hex helpers for the tool and the embedded key.
std::string HexEncode(const std::uint8_t* data, std::size_t len);
std::optional<std::array<std::uint8_t, 32>> HexDecode32(const std::string& hex);
std::optional<std::array<std::uint8_t, 64>> HexDecode64(const std::string& hex);

// Unix epoch seconds -> days since 2026-01-01 (the expiry_day unit).
inline std::int64_t UnixSecondsToLicenseDays(std::int64_t unix_s) {
    return unix_s / 86400 - kEpochDaysOffset;
}

}  // namespace droppoint::util::license
