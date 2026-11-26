// tools/license/main.cpp — droppoint-license, the vendor key tool.
//
//   droppoint-license keygen --out <dir>
//       writes <dir>/droppoint-license-private.key (hex, private — NEVER
//       ship) and prints the matching public key to paste into
//       packaging/license_public_key.txt.
//   droppoint-license make --key <private.key> --device DJI-XXXXX-XXXXX-XXXXX
//                          --days N [--out file.lic]
//       emits a "DPL1-..." license valid for N days from today.
//   droppoint-license inspect <DPL1-string>
//       prints the embedded expiry and the device code it binds to.
//
// The private key is the 64-byte Monocypher EdDSA secret key (seed ||
// public key) as 128 hex chars; the file also accepts a 64-char seed.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>
#    include <bcrypt.h>
#else
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

#include "monocypher.h"
#include "util/license.h"

namespace {

namespace lic = droppoint::util::license;

void Usage() {
    std::printf(
        "droppoint-license — DropPoint vendor license tool (KEEP PRIVATE)\n"
        "  droppoint-license keygen --out <dir>\n"
        "  droppoint-license make --key <private.key> --device DJI-XXXXX-XXXXX-XXXXX\n"
        "                         --days N [--out file.lic]\n"
        "  droppoint-license inspect <DPL1-string>\n");
}

bool RandomBytes(std::uint8_t* out, std::size_t n) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, out + got, n - got);
        if (r <= 0) {
            ::close(fd);
            return false;
        }
        got += static_cast<std::size_t>(r);
    }
    ::close(fd);
    return true;
#endif
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' ||
                          s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

// Loads a 64-byte secret key from a hex file: 128 chars (full Monocypher
// secret key) or 64 chars (seed only — the public half is regenerated).
std::optional<std::array<std::uint8_t, 64>> LoadSecretKey(const std::string& path) {
    const std::string hex = ReadTextFile(path);
    if (hex.size() == 128) {
        return lic::HexDecode64(hex);
    }
    if (hex.size() == 64) {
        const auto seed = lic::HexDecode32(hex);
        if (!seed.has_value()) return std::nullopt;
        std::array<std::uint8_t, 64> secret{};
        std::array<std::uint8_t, 32> pub{};
        std::array<std::uint8_t, 32> seed_copy = *seed;
        crypto_eddsa_key_pair(secret.data(), pub.data(), seed_copy.data());
        return secret;
    }
    return std::nullopt;
}

std::int64_t TodayLicenseDays() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const std::int64_t unix_s =
        std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return lic::UnixSecondsToLicenseDays(unix_s);
}

// Expiry day -> calendar date string (days since 2026-01-01).
std::string ExpiryDate(std::uint32_t day) {
    const std::int64_t unix_s = (static_cast<std::int64_t>(day) + lic::kEpochDaysOffset) * 86400;
    const std::time_t t = static_cast<std::time_t>(unix_s);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday);
    return buf;
}

int CmdKeygen(const std::string& out_dir) {
    if (out_dir.empty()) {
        std::fprintf(stderr, "keygen needs --out <dir>\n");
        return 2;
    }
    std::array<std::uint8_t, 32> seed{};
    if (!RandomBytes(seed.data(), seed.size())) {
        std::fprintf(stderr, "no OS random source available\n");
        return 1;
    }
    std::array<std::uint8_t, 64> secret{};
    std::array<std::uint8_t, 32> pub{};
    std::array<std::uint8_t, 32> seed_copy = seed;
    crypto_eddsa_key_pair(secret.data(), pub.data(), seed_copy.data());

    const std::string priv_path = out_dir + "/droppoint-license-private.key";
    {
        std::ofstream f(priv_path, std::ios::trunc);
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", priv_path.c_str());
            return 1;
        }
        f << lic::HexEncode(secret.data(), secret.size()) << "\n";
    }
#if !defined(_WIN32)
    ::chmod(priv_path.c_str(), 0600);
#endif
    std::printf("private key: %s (mode 600 — NEVER commit or share)\n", priv_path.c_str());
    std::printf("public key (paste into packaging/license_public_key.txt):\n%s\n",
                lic::HexEncode(pub.data(), pub.size()).c_str());
    return 0;
}

int CmdMake(const std::string& key_path, const std::string& device_code, long days,
            const std::string& out_path) {
    const auto secret = LoadSecretKey(key_path);
    if (!secret.has_value()) {
        std::fprintf(stderr, "cannot read a private key from '%s'\n", key_path.c_str());
        return 1;
    }
    if (device_code.rfind("DJI-", 0) != 0) {
        std::fprintf(stderr, "device code must look like DJI-XXXXX-XXXXX-XXXXX\n");
        return 2;
    }
    const auto hash = lic::DeviceCodeToHash(device_code);
    if (!hash.has_value()) {
        std::fprintf(stderr, "device code '%s' does not decode\n", device_code.c_str());
        return 2;
    }
    if (days <= 0 || days > 100000) {
        std::fprintf(stderr, "--days must be 1..100000\n");
        return 2;
    }
    const std::uint32_t expiry = static_cast<std::uint32_t>(TodayLicenseDays() + days);
    const std::string lic_text = lic::BuildLicenseString(*secret, *hash, expiry);
    std::printf("%s\n", lic_text.c_str());
    std::printf("  binds %s (hash prefix %s), expires %s (%u days, today is day %lld)\n",
                device_code.c_str(), lic::HexEncode(hash->data(), hash->size()).c_str(),
                ExpiryDate(expiry).c_str(), expiry, (long long)TodayLicenseDays());
    if (!out_path.empty()) {
        std::ofstream f(out_path, std::ios::trunc);
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
            return 1;
        }
        f << lic_text << "\n";
        std::printf("written: %s\n", out_path.c_str());
    }
    return 0;
}

int CmdInspect(const std::string& lic_text, const std::string& pubkey_hex) {
    const auto p = lic::ParseLicense(lic_text);
    if (!p.has_value()) {
        std::fprintf(stderr, "not a parseable DPL1- license string\n");
        return 1;
    }
    // Re-render the bound hash prefix as the device code's first 13 chars
    // (the display code carries a 9th byte this license does not store).
    std::array<std::uint8_t, 32> hash{};
    std::memcpy(hash.data(), p->device_hash.data(), p->device_hash.size());
    std::printf("version: %u\n", p->version);
    std::printf("device hash prefix: %s\n",
                lic::HexEncode(p->device_hash.data(), p->device_hash.size()).c_str());
    std::printf("device code prefix: %s\n",
                lic::Base32Encode(p->device_hash.data(), p->device_hash.size()).c_str());
    std::printf("expiry: day %u = %s\n", p->expiry_day, ExpiryDate(p->expiry_day).c_str());
    if (!pubkey_hex.empty()) {
        std::array<std::uint8_t, 32> dummy{};
        std::memcpy(dummy.data(), p->device_hash.data(), p->device_hash.size());
        // Verify signature only: feed the bound hash so a wrong-device
        // verdict can't mask a bad-signature check.
        const auto v = lic::VerifyLicense(*p, pubkey_hex, dummy, 0);
        std::printf("signature: %s\n",
                    v == lic::VerifyResult::kBadSignature ? "INVALID"
                    : v == lic::VerifyResult::kMalformed  ? "cannot check (bad pubkey)"
                                                          : "valid");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        Usage();
        return args.empty() ? 2 : 0;
    }

    const std::string cmd = args[0];
    std::string key_path, device_code, out_path, out_dir, lic_text, pubkey_hex;
    long days = -1;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--key" && i + 1 < args.size()) {
            key_path = args[++i];
        } else if (a == "--device" && i + 1 < args.size()) {
            device_code = args[++i];
        } else if (a == "--days" && i + 1 < args.size()) {
            days = std::atol(args[++i].c_str());
        } else if (a == "--out" && i + 1 < args.size()) {
            out_path = args[++i];
        } else if (a == "--pubkey" && i + 1 < args.size()) {
            pubkey_hex = args[++i];
        } else if (a.rfind("DPL1-", 0) == 0) {
            lic_text = a;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            Usage();
            return 2;
        }
    }

    if (cmd == "keygen") {
        return CmdKeygen(out_path);  // --out doubles as the output dir here
    }
    if (cmd == "make") {
        if (key_path.empty() || device_code.empty() || days < 0) {
            std::fprintf(stderr, "make needs --key, --device and --days\n");
            return 2;
        }
        return CmdMake(key_path, device_code, days, out_path);
    }
    if (cmd == "inspect") {
        if (lic_text.empty()) {
            std::fprintf(stderr, "inspect needs a DPL1- string argument\n");
            return 2;
        }
        return CmdInspect(lic_text, pubkey_hex);
    }
    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    Usage();
    return 2;
}
