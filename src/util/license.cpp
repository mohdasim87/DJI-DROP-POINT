// util/license.cpp — see header.
#include "license.h"

#include <cstring>

#include "monocypher.h"

namespace droppoint::util::license {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int Base32Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

std::array<std::uint8_t, kHeaderBytes> HeaderBytes(const Payload& p) {
    std::array<std::uint8_t, kHeaderBytes> h{};
    h[0] = p.version;
    std::memcpy(h.data() + 1, p.device_hash.data(), kDeviceHashBoundBytes);
    h[9] = static_cast<std::uint8_t>(p.expiry_day & 0xff);
    h[10] = static_cast<std::uint8_t>((p.expiry_day >> 8) & 0xff);
    h[11] = static_cast<std::uint8_t>((p.expiry_day >> 16) & 0xff);
    h[12] = static_cast<std::uint8_t>((p.expiry_day >> 24) & 0xff);
    return h;
}

}  // namespace

std::string Base32Encode(const std::uint8_t* data, std::size_t len) {
    std::string out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (std::size_t i = 0; i < len; ++i) {
        acc = (acc << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            out.push_back(kAlphabet[(acc >> (bits - 5)) & 0x1f]);
            bits -= 5;
        }
    }
    if (bits > 0) {
        out.push_back(kAlphabet[(acc << (5 - bits)) & 0x1f]);
    }
    return out;
}

std::optional<std::string> Base32Decode(const std::string& text) {
    std::string out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        const int v = Base32Value(c);
        if (v < 0) return std::nullopt;
        acc = (acc << 5) | static_cast<std::uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            out.push_back(static_cast<char>((acc >> (bits - 8)) & 0xff));
            bits -= 8;
        }
    }
    // Leftover bits must be zero padding (RFC 4648 "pad bits MUST be zero").
    if (bits > 0 && (acc & ((1u << bits) - 1u)) != 0) return std::nullopt;
    return out;
}

std::string DeviceCodeFromHash(const std::array<std::uint8_t, 32>& device_hash) {
    const std::string b32 = Base32Encode(device_hash.data(), kDeviceCodeBytes);
    std::string out = "DJI-";
    for (std::size_t i = 0; i < b32.size(); ++i) {
        if (i > 0 && i % 5 == 0) out.push_back('-');
        out.push_back(b32[i]);
    }
    return out;
}

std::optional<std::array<std::uint8_t, kDeviceHashBoundBytes>> DeviceCodeToHash(
    const std::string& code) {
    std::string digits;
    std::string body = code;
    if (body.rfind("DJI-", 0) == 0 || body.rfind("dji-", 0) == 0) {
        body = body.substr(4);
    }
    for (const char c : body) {
        if (c == '-' || c == ' ') continue;
        digits.push_back(c);
    }
    const auto decoded = Base32Decode(digits);
    if (!decoded.has_value() || decoded->size() < kDeviceHashBoundBytes) {
        return std::nullopt;
    }
    std::array<std::uint8_t, kDeviceHashBoundBytes> out{};
    std::memcpy(out.data(), decoded->data(), kDeviceHashBoundBytes);
    return out;
}

std::optional<Payload> ParseLicense(const std::string& text) {
    std::string body = text;
    // Tolerate surrounding whitespace/newlines from a copied .lic file.
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' ||
                             body.back() == ' ' || body.back() == '\t')) {
        body.pop_back();
    }
    while (!body.empty() && (body.front() == ' ' || body.front() == '\t')) {
        body.erase(body.begin());
    }
    if (body.rfind("DPL1-", 0) != 0) return std::nullopt;
    const auto decoded = Base32Decode(body.substr(5));
    if (!decoded.has_value() || decoded->size() != kPayloadBytes) return std::nullopt;
    const auto* b = reinterpret_cast<const std::uint8_t*>(decoded->data());
    if (b[0] != kLicenseVersion) return std::nullopt;
    Payload p;
    p.version = b[0];
    std::memcpy(p.device_hash.data(), b + 1, kDeviceHashBoundBytes);
    p.expiry_day = static_cast<std::uint32_t>(b[9]) |
                   (static_cast<std::uint32_t>(b[10]) << 8) |
                   (static_cast<std::uint32_t>(b[11]) << 16) |
                   (static_cast<std::uint32_t>(b[12]) << 24);
    std::memcpy(p.signature.data(), b + kHeaderBytes, 64);
    return p;
}

std::string FormatLicense(const Payload& payload) {
    std::array<std::uint8_t, kPayloadBytes> bin{};
    const auto header = HeaderBytes(payload);
    std::memcpy(bin.data(), header.data(), header.size());
    std::memcpy(bin.data() + kHeaderBytes, payload.signature.data(), 64);
    return "DPL1-" + Base32Encode(bin.data(), bin.size());
}

std::string BuildLicenseString(
    const std::array<std::uint8_t, 64>& secret_key,
    const std::array<std::uint8_t, kDeviceHashBoundBytes>& device_hash,
    std::uint32_t expiry_day) {
    Payload p;
    p.device_hash = device_hash;
    p.expiry_day = expiry_day;
    const auto header = HeaderBytes(p);
    crypto_eddsa_sign(p.signature.data(), secret_key.data(), header.data(), header.size());
    return FormatLicense(p);
}

VerifyResult VerifyLicense(const Payload& payload, const std::string& public_key_hex,
                           const std::array<std::uint8_t, 32>& device_hash,
                           std::int64_t today_days) {
    const auto pub = HexDecode32(public_key_hex);
    if (!pub.has_value()) return VerifyResult::kMalformed;
    bool all_zero = true;
    for (const std::uint8_t b : *pub) all_zero &= (b == 0);
    if (all_zero) return VerifyResult::kMalformed;  // no key embedded: fail closed

    const auto header = HeaderBytes(payload);
    if (crypto_eddsa_check(payload.signature.data(), pub->data(), header.data(),
                           header.size()) != 0) {
        return VerifyResult::kBadSignature;
    }
    if (std::memcmp(payload.device_hash.data(), device_hash.data(),
                    kDeviceHashBoundBytes) != 0) {
        return VerifyResult::kWrongDevice;
    }
    if (static_cast<std::int64_t>(payload.expiry_day) < today_days) {
        return VerifyResult::kExpired;
    }
    return VerifyResult::kValid;
}

std::string HexEncode(const std::uint8_t* data, std::size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

namespace {

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> HexDecodeN(const std::string& hex) {
    auto nyb = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (hex.size() != N * 2) return std::nullopt;
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const int hi = nyb(hex[i * 2]);
        const int lo = nyb(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

}  // namespace

std::optional<std::array<std::uint8_t, 32>> HexDecode32(const std::string& hex) {
    return HexDecodeN<32>(hex);
}

std::optional<std::array<std::uint8_t, 64>> HexDecode64(const std::string& hex) {
    return HexDecodeN<64>(hex);
}

}  // namespace droppoint::util::license
