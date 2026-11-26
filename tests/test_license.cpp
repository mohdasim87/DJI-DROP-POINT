// tests/test_license.cpp
//
// Licensing: SHA-256 vectors, base32/device-code codec, Ed25519 license
// roundtrip (throwaway key generated in-test), all VerifyResult branches,
// the trial clock (first run, warn boundary, expiry, rollback, deleted
// state, unwritable dir), device_id against injected roots, and
// InstallLicense/CheckLicense over a DP_TEST_ROOT-style temp tree.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_framework.h"

#include "core/gating.h"
#include "hw/device_id.h"
#include "hw/license_state.h"
#include "monocypher.h"
#include "util/license.h"
#include "util/sha256.h"

namespace {

namespace lic = droppoint::util::license;
using droppoint::hw::CheckLicense;
using droppoint::hw::InstallLicense;
using droppoint::hw::LicenseKind;
using droppoint::hw::UpdateTrialState;

std::filesystem::path g_root;

std::filesystem::path FreshDir(const std::string& name) {
    const auto p = g_root / name;
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p;
}

void WriteFile(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << content;
}

// A throwaway Ed25519 key pair fixed by a constant seed — never the real
// vendor key.
struct TestKeys {
    std::array<std::uint8_t, 64> secret{};
    std::array<std::uint8_t, 32> pub{};
    std::string pub_hex;
};
TestKeys MakeKeys(std::uint8_t tag = 0x42) {
    TestKeys k;
    std::array<std::uint8_t, 32> seed{};
    seed.fill(tag);
    crypto_eddsa_key_pair(k.secret.data(), k.pub.data(), seed.data());
    k.pub_hex = lic::HexEncode(k.pub.data(), k.pub.size());
    return k;
}

std::array<std::uint8_t, 32> FakeDeviceHash(std::uint8_t tag = 0x11) {
    std::array<std::uint8_t, 32> h{};
    h.fill(tag);
    return h;
}

std::string FakeDeviceCode(std::uint8_t tag = 0x11) {
    return lic::DeviceCodeFromHash(FakeDeviceHash(tag));
}

}  // namespace

TEST_CASE("sha256 known vectors") {
    REQUIRE(droppoint::util::Sha256Hex("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(droppoint::util::Sha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(droppoint::util::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnop"
                                       "nopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("base32 roundtrip and alphabet") {
    REQUIRE(lic::Base32Encode(reinterpret_cast<const std::uint8_t*>("f"), 1) == "MY");
    REQUIRE(lic::Base32Encode(reinterpret_cast<const std::uint8_t*>("fo"), 2) == "MZXQ");
    REQUIRE(lic::Base32Encode(reinterpret_cast<const std::uint8_t*>("foo"), 3) == "MZXW6");
    REQUIRE(lic::Base32Encode(reinterpret_cast<const std::uint8_t*>("foob"), 4) == "MZXW6YQ");
    REQUIRE(lic::Base32Encode(reinterpret_cast<const std::uint8_t*>("fooba"), 5) ==
            "MZXW6YTB");
    REQUIRE(lic::Base32Encode(reinterpret_cast<const std::uint8_t*>("foobar"), 6) ==
            "MZXW6YTBOI");
    const std::string bytes = "drop-point-license-payload-bytes";
    const auto enc =
        lic::Base32Encode(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    const auto dec = lic::Base32Decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == bytes);
    REQUIRE(!lic::Base32Decode("MZXW6!").has_value());      // bad char
    REQUIRE(!lic::Base32Decode("MZ").has_value());          // nonzero pad bits
}

TEST_CASE("device code format and roundtrip") {
    const auto h = FakeDeviceHash();
    const std::string code = lic::DeviceCodeFromHash(h);
    REQUIRE(code.size() == std::string("DJI-XXXXX-XXXXX-XXXXX").size());
    REQUIRE(code.rfind("DJI-", 0) == 0);
    REQUIRE(code[9] == '-' && code[15] == '-');
    const auto back = lic::DeviceCodeToHash(code);
    REQUIRE(back.has_value());
    for (int i = 0; i < 8; ++i) REQUIRE((*back)[i] == h[i]);
    REQUIRE(!lic::DeviceCodeToHash("DJI-XXXXX-XXXXX-XXX!").has_value());
    REQUIRE(!lic::DeviceCodeToHash("DJI-AAAA").has_value());
    // lowercase + spaces tolerated
    std::string messy = "dji-";
    for (char c : code.substr(4)) messy.push_back(static_cast<char>(std::tolower(c)));
    REQUIRE(lic::DeviceCodeToHash(messy).has_value());
}

TEST_CASE("license build/parse/verify roundtrip") {
    const auto keys = MakeKeys();
    const auto h = FakeDeviceHash();
    std::array<std::uint8_t, 8> prefix{};
    std::copy_n(h.begin(), 8, prefix.begin());
    const std::string text = lic::BuildLicenseString(keys.secret, prefix, 100);
    REQUIRE(text.rfind("DPL1-", 0) == 0);
    const auto p = lic::ParseLicense(text);
    REQUIRE(p.has_value());
    REQUIRE(p->version == 1);
    REQUIRE(p->device_hash == prefix);
    REQUIRE(p->expiry_day == 100);
    REQUIRE(lic::VerifyLicense(*p, keys.pub_hex, h, 50) == lic::VerifyResult::kValid);
    REQUIRE(lic::VerifyLicense(*p, keys.pub_hex, h, 100) == lic::VerifyResult::kValid);
}

TEST_CASE("license verify rejects") {
    const auto keys = MakeKeys();
    const auto h = FakeDeviceHash();
    std::array<std::uint8_t, 8> prefix{};
    std::copy_n(h.begin(), 8, prefix.begin());
    const std::string text = lic::BuildLicenseString(keys.secret, prefix, 100);
    const auto p = lic::ParseLicense(text);
    REQUIRE(p.has_value());

    REQUIRE(lic::VerifyLicense(*p, keys.pub_hex, h, 101) == lic::VerifyResult::kExpired);
    REQUIRE(lic::VerifyLicense(*p, keys.pub_hex, FakeDeviceHash(0x22), 50) ==
            lic::VerifyResult::kWrongDevice);

    // flipped character in the string -> parse or signature failure
    std::string tampered = text;
    tampered[tampered.size() - 2] = (tampered[tampered.size() - 2] == 'A') ? 'B' : 'A';
    const auto tp = lic::ParseLicense(tampered);
    if (tp.has_value()) {
        REQUIRE(lic::VerifyLicense(*tp, keys.pub_hex, h, 50) ==
                lic::VerifyResult::kBadSignature);
    }
    // tampered payload bit, same shape
    auto p2 = *p;
    p2.expiry_day = 99999;
    REQUIRE(lic::VerifyLicense(p2, keys.pub_hex, h, 50) ==
            lic::VerifyResult::kBadSignature);

    // malformed strings
    REQUIRE(!lic::ParseLicense("").has_value());
    REQUIRE(!lic::ParseLicense("DPL1-").has_value());
    REQUIRE(!lic::ParseLicense("XXXX-AAAA").has_value());
    REQUIRE(!lic::ParseLicense("DPL1-AAAA").has_value());

    // zero/missing embedded key fails closed
    REQUIRE(lic::VerifyLicense(*p, std::string(64, '0'), h, 50) ==
            lic::VerifyResult::kMalformed);
    REQUIRE(lic::VerifyLicense(*p, "nothex", h, 50) == lic::VerifyResult::kMalformed);

    // wrong vendor key -> bad signature
    const auto other = MakeKeys(0x99);
    REQUIRE(lic::VerifyLicense(*p, other.pub_hex, h, 50) ==
            lic::VerifyResult::kBadSignature);
}

TEST_CASE("device_id from injected roots") {
    // Raspberry Pi devicetree serial wins.
    auto root = FreshDir("dev-pi").string();
    WriteFile(std::filesystem::path(root) / "sys/firmware/devicetree/base/serial-number",
              std::string("pi-serial-123\0\0", 15));
    WriteFile(std::filesystem::path(root) / "etc/machine-id", "machine-id-456\n");
    const auto h1 = droppoint::hw::ComputeDeviceHash(root);
    REQUIRE(h1.has_value());
    // NUL-stripped content is hashed.
    REQUIRE(*h1 == droppoint::util::Sha256("pi-serial-123"));
    REQUIRE(droppoint::hw::DeviceCode(root).rfind("DJI-", 0) == 0);

    // machine-id used when no devicetree node.
    root = FreshDir("dev-mid").string();
    WriteFile(std::filesystem::path(root) / "etc/machine-id", "machine-id-456\n");
    const auto h2 = droppoint::hw::ComputeDeviceHash(root);
    REQUIRE(h2.has_value());
    REQUIRE(*h2 == droppoint::util::Sha256("machine-id-456"));

    // dbus machine-id fallback (Manifold 3 container without /etc/machine-id).
    root = FreshDir("dev-dbus").string();
    WriteFile(std::filesystem::path(root) / "var/lib/dbus/machine-id", "dbus-id-789\n");
    const auto h3 = droppoint::hw::ComputeDeviceHash(root);
    REQUIRE(h3.has_value());
    REQUIRE(*h3 == droppoint::util::Sha256("dbus-id-789"));

    // first non-loopback MAC; lo skipped.
    root = FreshDir("dev-mac").string();
    WriteFile(std::filesystem::path(root) / "sys/class/net/lo/address", "00:00:00:00:00:00\n");
    WriteFile(std::filesystem::path(root) / "sys/class/net/eth0/address",
              "aa:bb:cc:dd:ee:ff\n");
    const auto h4 = droppoint::hw::ComputeDeviceHash(root);
    REQUIRE(h4.has_value());
    REQUIRE(*h4 == droppoint::util::Sha256("aa:bb:cc:dd:ee:ff"));

    // nothing -> DJI-UNAVAILABLE
    root = FreshDir("dev-none").string();
    std::filesystem::create_directories(std::filesystem::path(root) / "sys/class/net");
    REQUIRE(!droppoint::hw::ComputeDeviceHash(root).has_value());
    REQUIRE(droppoint::hw::DeviceCode(root) == "DJI-UNAVAILABLE");
}

TEST_CASE("trial clock") {
    using droppoint::hw::kTrialDays;
    const std::string dir = FreshDir("trial").string();
    const std::string mirror = (std::filesystem::path(dir) / "mirror").string();

    bool persistent = false;
    auto st = UpdateTrialState(dir, mirror, 0, &persistent);   // first run
    REQUIRE(persistent);
    REQUIRE(st.first_seen_days == 0);
    REQUIRE(st.last_seen_days == 0);

    st = UpdateTrialState(dir, mirror, 19, &persistent);       // day 19: no warn yet
    REQUIRE(st.last_seen_days - st.first_seen_days == 19);
    st = UpdateTrialState(dir, mirror, 20, &persistent);       // day 20: warn boundary
    REQUIRE(st.last_seen_days - st.first_seen_days == 20);
    st = UpdateTrialState(dir, mirror, 29, &persistent);
    REQUIRE(st.last_seen_days - st.first_seen_days == 29);
    st = UpdateTrialState(dir, mirror, 30, &persistent);       // expired
    REQUIRE(st.last_seen_days - st.first_seen_days == kTrialDays);

    // Clock rollback: last_seen clamps forward, trial never extends.
    st = UpdateTrialState(dir, mirror, 5, &persistent);
    REQUIRE(st.last_seen_days == 30);
    REQUIRE(st.last_seen_days - st.first_seen_days == 30);

    // Mirror survives a deleted primary (earlier first_seen wins).
    const std::string dir2 = FreshDir("trial2").string();
    const std::string mirror2 = (std::filesystem::path(dir2) / "m").string();
    UpdateTrialState(dir2, mirror2, 0, &persistent);
    UpdateTrialState(dir2, mirror2, 10, &persistent);
    std::filesystem::remove(std::filesystem::path(dir2) / "license.state");
    st = UpdateTrialState(dir2, mirror2, 11, &persistent);
    REQUIRE(st.first_seen_days == 0);  // from the mirror
    REQUIRE(st.last_seen_days == 11);

    // Both deleted -> first_seen resets (accepted limitation, see manual).
    std::filesystem::remove(std::filesystem::path(dir2) / "license.state");
    std::filesystem::remove(std::filesystem::path(mirror2) / ".lic_state");
    st = UpdateTrialState(dir2, mirror2, 40, &persistent);
    REQUIRE(st.first_seen_days == 40);

    // Unwritable dirs: no crash, persistent=false.
    const std::string dir3 = FreshDir("trial3").string();
    const std::string file_as_dir = (std::filesystem::path(dir3) / "afile").string();
    WriteFile(file_as_dir, "x");
    st = UpdateTrialState(file_as_dir, file_as_dir, 3, &persistent);
    REQUIRE(!persistent);
    REQUIRE(st.first_seen_days == 3);
}

TEST_CASE("CheckLicense full paths") {
    const auto keys = MakeKeys();
    const std::string root = FreshDir("licroot").string();
    const std::string cfg = FreshDir("liccfg").string();
    // device identity via machine-id in the fake root
    WriteFile(std::filesystem::path(root) / "etc/machine-id", "lic-machine-1\n");

    // trial: fresh -> kTrial with 30 days
    auto s = CheckLicense(cfg, keys.pub_hex, 0, root);
    REQUIRE(s.kind == LicenseKind::kTrial);
    REQUIRE(s.days_left == 30);
    REQUIRE(s.device_code.rfind("DJI-", 0) == 0);

    // expired trial
    s = CheckLicense(cfg, keys.pub_hex, 30, root);
    REQUIRE(s.kind == LicenseKind::kExpired);

    // valid license binds it, even after expiry
    const auto hash = droppoint::hw::ComputeDeviceHash(root);
    REQUIRE(hash.has_value());
    const std::string code = lic::DeviceCodeFromHash(*hash);
    const auto prefix = lic::DeviceCodeToHash(code);
    REQUIRE(prefix.has_value());
    WriteFile(std::filesystem::path(cfg) / "license.key",
              lic::BuildLicenseString(keys.secret, *prefix, 500));
    s = CheckLicense(cfg, keys.pub_hex, 30, root);
    REQUIRE(s.kind == LicenseKind::kLicensed);

    // malformed license file reported, not skipped
    WriteFile(std::filesystem::path(cfg) / "license.key", "GARBAGE");
    s = CheckLicense(cfg, keys.pub_hex, 0, root);
    REQUIRE(s.kind == LicenseKind::kBadLicense);
    REQUIRE(s.reason.find("malformed") != std::string::npos);
    std::filesystem::remove(std::filesystem::path(cfg) / "license.key");

    // license for another device — fresh root so the trial clock restarts
    const std::string root3 = FreshDir("licroot3").string();
    WriteFile(std::filesystem::path(root3) / "etc/machine-id", "lic-machine-3\n");
    const auto hash3 = droppoint::hw::ComputeDeviceHash(root3);
    const auto other_prefix = lic::DeviceCodeToHash(lic::DeviceCodeFromHash(*hash3));
    auto wrong_prefix = *other_prefix;
    wrong_prefix[0] ^= 0xff;
    WriteFile(std::filesystem::path(cfg) / "license.key",
              lic::BuildLicenseString(keys.secret, wrong_prefix, 500));
    s = CheckLicense(cfg, keys.pub_hex, 0, root3);
    // wrong device but still inside trial -> trial keeps running, reason kept
    REQUIRE(s.kind == LicenseKind::kTrial);
    REQUIRE(!s.reason.empty());
    s = CheckLicense(cfg, keys.pub_hex, 30, root3);
    REQUIRE(s.kind == LicenseKind::kBadLicense);
    std::filesystem::remove(std::filesystem::path(cfg) / "license.key");

    // no device identity at all + expired trial -> kUnlicensedNoId
    const std::string root2 = FreshDir("licroot2").string();
    std::filesystem::create_directories(std::filesystem::path(root2) / "sys/class/net");
    // age the trial instantly: seed the state at day 0, check at day 31
    UpdateTrialState((std::filesystem::path(root2) / "var/lib/droppoint").string(),
                     (std::filesystem::path(root2) / "var/log/droppoint").string(), 0,
                     nullptr);
    s = CheckLicense(cfg, keys.pub_hex, 31, root2);
    REQUIRE(s.kind == LicenseKind::kUnlicensedNoId);
    REQUIRE(s.device_code == "DJI-UNAVAILABLE");
}

TEST_CASE("InstallLicense validates then writes") {
    const auto keys = MakeKeys();
    const std::string root = FreshDir("instroot").string();
    const std::string cfg = FreshDir("instcfg").string();
    WriteFile(std::filesystem::path(root) / "etc/machine-id", "inst-machine\n");
    const auto hash = droppoint::hw::ComputeDeviceHash(root);
    const auto prefix = lic::DeviceCodeToHash(lic::DeviceCodeFromHash(*hash));

    auto r = InstallLicense("not-a-license", cfg, keys.pub_hex, 0, root);
    REQUIRE(!r.ok);

    // expired for this device
    auto bad = lic::BuildLicenseString(keys.secret, *prefix, 10);
    r = InstallLicense(bad, cfg, keys.pub_hex, 20, root);
    REQUIRE(!r.ok);

    const std::string good = lic::BuildLicenseString(keys.secret, *prefix, 500);
    r = InstallLicense(good, cfg, keys.pub_hex, 0, root);
    REQUIRE(r.ok);
    REQUIRE(r.path.find("license.key") != std::string::npos);
    REQUIRE(std::filesystem::exists(r.path));
    // /etc in the fake root should be preferred
    REQUIRE(std::filesystem::exists(std::filesystem::path(root) / "etc" / "droppoint" /
                                    "license.key"));
    auto s = CheckLicense(cfg, keys.pub_hex, 0, root);
    REQUIRE(s.kind == LicenseKind::kLicensed);
}

TEST_CASE("license gating: expired trips the hard hold, abort unaffected") {
    droppoint::core::gating::Inputs in;
    in.target_latched = true;
    in.target_confirmed = true;
    in.license_ok = false;
    const auto eval = droppoint::core::gating::Evaluate(in, {});
    REQUIRE(eval.any_hard_tripped);
    REQUIRE(eval.primary_hold_reason == "LICENSE EXPIRED - CONTACT VENDOR");
    in.license_ok = true;
    const auto ok_eval = droppoint::core::gating::Evaluate(in, {});
    REQUIRE(!ok_eval.any_hard_tripped ||
            ok_eval.primary_hold_reason != "LICENSE EXPIRED - CONTACT VENDOR");
}

struct LicenseTestInit {
    LicenseTestInit() {
        g_root = std::filesystem::temp_directory_path() / "droppoint_license_tests";
        std::filesystem::create_directories(g_root);
    }
} g_license_test_init;
