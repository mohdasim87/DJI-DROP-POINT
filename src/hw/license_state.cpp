// hw/license_state.cpp — see header.
#include "license_state.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "device_id.h"
#include "file_ownership.h"
#include "util/license.h"
#include "util/logging.h"

namespace droppoint::hw {

namespace fs = std::filesystem;

namespace {

std::string Join(const std::string& root, const std::string& abs) {
    return root.empty() ? abs : (fs::path(root) / abs.substr(1)).string();
}

std::optional<TrialState> ReadStateFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    TrialState st;
    std::string key;
    std::int64_t value = 0;
    bool got_first = false, got_last = false;
    while (f >> key) {
        if (f >> value) {
            if (key == "first_seen") {
                st.first_seen_days = value;
                got_first = true;
            } else if (key == "last_seen") {
                st.last_seen_days = value;
                got_last = true;
            }
        }
    }
    if (!got_first && !got_last) return std::nullopt;
    return st;
}

bool WriteStateFile(const std::string& path, const TrialState& st) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "first_seen " << st.first_seen_days << "\n"
      << "last_seen " << st.last_seen_days << "\n";
    f.close();
    // A root run (--selftest, --license-status) must not leave files the
    // service user cannot rewrite — adopt the state dir's owner.
    AdoptOwnerFromDir(path, fs::path(path).parent_path().string());
    return f.good();
}

struct LicenseFile {
    bool found = false;
    bool parse_error = false;
    std::string path;
    droppoint::util::license::Payload payload;
};

LicenseFile LoadLicenseFile(const std::string& config_dir, const std::string& root) {
    const std::vector<std::string> candidates = {
        Join(root, "/etc/droppoint/license.key"),
        (fs::path(config_dir) / "license.key").string(),
    };
    LicenseFile lf;
    for (const auto& path : candidates) {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        lf.found = true;
        lf.path = path;
        std::ifstream f(path);
        std::ostringstream ss;
        ss << f.rdbuf();
        const auto parsed = droppoint::util::license::ParseLicense(ss.str());
        if (!parsed.has_value()) {
            lf.parse_error = true;  // malformed file is reported, not skipped
            DP_LOG_ERROR("license", "malformed license file: " + path);
        } else {
            lf.payload = *parsed;
        }
        return lf;  // first found wins, malformed or not
    }
    return lf;
}

}  // namespace

TrialState UpdateTrialState(const std::string& state_dir, const std::string& mirror_dir,
                            std::int64_t today_days, bool* persistent) {
    const std::string a = (fs::path(state_dir) / "license.state").string();
    const std::string b = (fs::path(mirror_dir) / ".lic_state").string();

    const auto sa = ReadStateFile(a);
    const auto sb = ReadStateFile(b);
    TrialState st;
    bool have = false;
    for (const auto& s : {sa, sb}) {
        if (!s.has_value()) continue;
        if (!have) {
            st = *s;
            have = true;
        } else {
            // Anti-rollback: the earlier first_seen always wins.
            st.first_seen_days = std::min(st.first_seen_days, s->first_seen_days);
            st.last_seen_days = std::max(st.last_seen_days, s->last_seen_days);
        }
    }
    if (!have) {
        st.first_seen_days = today_days;
        st.last_seen_days = today_days;
    }
    // Clock rollback: last_seen never moves backwards, so a rolled-back
    // clock cannot stretch the trial.
    st.last_seen_days = std::max(st.last_seen_days, today_days);

    const bool wa = WriteStateFile(a, st);
    const bool wb = WriteStateFile(b, st);
    const bool ok = wa || wb;
    if (persistent != nullptr) *persistent = ok;
    if (!ok) {
        DP_LOG_WARN("license", "trial state not writable (" + state_dir + ", " + mirror_dir +
                                   ") — trial runs from process start only");
    }
    return st;
}

LicenseStatus CheckLicense(const std::string& config_dir, const std::string& public_key_hex,
                           std::int64_t today_days, const std::string& root) {
    LicenseStatus st;
    const auto hash = ComputeDeviceHash(root);
    st.device_code = hash.has_value() ? droppoint::util::license::DeviceCodeFromHash(*hash)
                                    : "DJI-UNAVAILABLE";

    const LicenseFile lf = LoadLicenseFile(config_dir, root);
    st.license_path = lf.path;

    if (lf.found) {
        if (lf.parse_error) {
            st.kind = LicenseKind::kBadLicense;
            st.reason = "malformed license file";
            return st;
        }
        if (!hash.has_value()) {
            // No device identity: the license cannot be checked for binding
            // — fail closed.
            st.kind = LicenseKind::kBadLicense;
            st.reason = "no device identity to bind against";
            return st;
        }
        using V = droppoint::util::license::VerifyResult;
        const V v = droppoint::util::license::VerifyLicense(lf.payload, public_key_hex,
                                                          *hash, today_days);
        switch (v) {
            case V::kValid:
                st.kind = LicenseKind::kLicensed;
                return st;
            case V::kExpired:
                st.kind = LicenseKind::kBadLicense;
                st.reason = "license expired";
                break;
            case V::kWrongDevice:
                st.kind = LicenseKind::kBadLicense;
                st.reason = "license is for a different device";
                break;
            case V::kBadSignature:
                st.kind = LicenseKind::kBadLicense;
                st.reason = "bad license signature";
                break;
            case V::kMalformed:
                st.kind = LicenseKind::kBadLicense;
                st.reason = "license malformed or vendor key missing";
                break;
        }
        // A bad license file does NOT fall back to the trial: that would
        // let an expired license be neutralised by ignoring it — but it
        // also shouldn't grant trial days the device never had. Treat as
        // expired when the trial is over, else keep the trial running and
        // surface the reason.
    }

    // Trial path (no file, or a bad one while trial time remains).
    const std::string state_dir = Join(root, "/var/lib/droppoint");
    const std::string mirror_dir = Join(root, "/var/log/droppoint");
    bool persistent = true;
    TrialState ts = UpdateTrialState(state_dir, mirror_dir, today_days, &persistent);
    if (!persistent) {
        // Manifold 3 (.dpk): the app cannot write /var; its own data/ dir
        // persists across restarts, so the trial survives there.
        const std::string app_data = (fs::path(config_dir) / ".." / "data").string();
        ts = UpdateTrialState(app_data, app_data, today_days, &persistent);
    }
    st.trial_persistent = persistent;
    const std::int64_t used = ts.last_seen_days - ts.first_seen_days;
    if (used < kTrialDays) {
        st.kind = LicenseKind::kTrial;
        st.days_left = static_cast<int>(kTrialDays - used);
        return st;
    }
    if (lf.found && st.kind == LicenseKind::kBadLicense) {
        return st;  // bad license + trial over: keep the precise reason
    }
    st.kind = hash.has_value() ? LicenseKind::kExpired : LicenseKind::kUnlicensedNoId;
    return st;
}

std::string EffectivePublicKeyHex(const std::string& embedded_hex) {
    const char* env = std::getenv("DP_LICENSE_PUBKEY_HEX");
    if (env != nullptr && *env != '\0') {
        return env;
    }
    return embedded_hex;
}

LicenseStatus CheckLicenseNow(const std::string& config_dir,
                              const std::string& public_key_hex,
                              std::int64_t now_unix_s) {
    return CheckLicense(config_dir, EffectivePublicKeyHex(public_key_hex),
                        droppoint::util::license::UnixSecondsToLicenseDays(now_unix_s),
                        DeviceIdRoot());
}

InstallResult InstallLicense(const std::string& license_text, const std::string& config_dir,
                             const std::string& public_key_hex, std::int64_t today_days,
                             const std::string& root) {
    InstallResult r;
    const auto payload = droppoint::util::license::ParseLicense(license_text);
    if (!payload.has_value()) {
        r.error = "not a DPL1- license string";
        return r;
    }
    const auto hash = ComputeDeviceHash(root);
    if (!hash.has_value()) {
        r.error = "no device identity on this machine — a license cannot be bound";
        return r;
    }
    using V = droppoint::util::license::VerifyResult;
    const V v = droppoint::util::license::VerifyLicense(
        *payload, EffectivePublicKeyHex(public_key_hex), *hash, today_days);
    switch (v) {
        case V::kValid: break;
        case V::kExpired: r.error = "license is expired"; return r;
        case V::kWrongDevice: r.error = "license is for a different device"; return r;
        case V::kBadSignature: r.error = "bad license signature"; return r;
        case V::kMalformed: r.error = "license malformed or vendor key missing"; return r;
    }

    const std::string etc_path = Join(root, "/etc/droppoint/license.key");
    const std::string cfg_path = (fs::path(config_dir) / "license.key").string();
    // Normalise to a single line.
    std::string text = license_text;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' ||
                             text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    text.push_back('\n');
    for (const std::string& path : {etc_path, cfg_path}) {
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream f(path, std::ios::trunc);
        if (!f) continue;
        f << text;
        f.close();
        if (!f.good()) continue;
        r.ok = true;
        r.path = path;
        return r;
    }
    r.error = "could not write " + etc_path + " or " + cfg_path;
    return r;
}

LicenseMonitor::LicenseMonitor(std::string config_dir, std::string public_key_hex,
                               std::string root)
    : config_dir_(std::move(config_dir)),
      pubkey_(std::move(public_key_hex)),
      root_(std::move(root)) {}

const LicenseStatus& LicenseMonitor::Poll(std::int64_t now_unix_s) {
    const std::int64_t today = droppoint::util::license::UnixSecondsToLicenseDays(now_unix_s);
    if (first_) {
        first_ = false;
        last_day_ = today;
        status_ = CheckLicense(config_dir_, pubkey_, today, root_);
        watched_path_ = status_.license_path;
        have_watch_ = false;
        return status_;
    }
    // Re-verify only when the license file (or its absence) changed on
    // disk — a stat per loop cycle is cheap, a full re-check is not free.
    std::string current;
    for (const std::string& p : {Join(root_, "/etc/droppoint/license.key"),
                                 (fs::path(config_dir_) / "license.key").string()}) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            current = p;
            break;
        }
    }
    bool changed = current != watched_path_;
    std::uint64_t mtime_ns = 0;
    std::uintmax_t size = 0;
    if (!current.empty()) {
        std::error_code ec;
        const auto t = fs::last_write_time(current, ec);
        if (!ec) mtime_ns = static_cast<std::uint64_t>(t.time_since_epoch().count());
        const auto s = fs::file_size(current, ec);
        if (!ec) size = s;
    }
    if (!changed && have_watch_ && (mtime_ns != watched_mtime_ns_ || size != watched_size_)) {
        changed = true;
    }
    if (!changed) {
        // Day boundary flips a trial countdown / expiry without any file change.
        if (today == last_day_) {
            return status_;
        }
        last_day_ = today;
    }
    watched_path_ = current;
    watched_mtime_ns_ = mtime_ns;
    watched_size_ = size;
    have_watch_ = true;
    status_ = CheckLicense(config_dir_, pubkey_, today, root_);
    return status_;
}

}  // namespace droppoint::hw
