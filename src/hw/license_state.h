// hw/license_state.h
//
// Trial clock + license-file resolution for the shipped binary.
//
// Trial state lives in TWO files so a deleted state file cannot silently
// reset the trial: <state_dir>/license.state and its mirror
// <mirror_dir>/.lic_state (production: /var/lib/droppoint and
// /var/log/droppoint). On read the EARLIER first_seen wins
// (anti-rollback); a wall clock behind last_seen never shortens the
// trial. Format: "first_seen=<unix days>\nlast_seen=<unix days>\n".
//
// License file search order (first found wins):
//   <root>/etc/droppoint/license.key        — .deb install
//   <config_dir>/license.key                — Manifold 3 .dpk app dir
// A file that exists but does not parse is reported, never skipped.
//
// LicenseStatus::CheckLicense is called at startup and cheaply re-checked
// each loop cycle via LicenseMonitor (re-reads only on mtime change).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace droppoint::hw {

// Everything the trial clock persists.
struct TrialState {
    std::int64_t first_seen_days = 0;  // unix days
    std::int64_t last_seen_days = 0;
};

// Reads whichever state files exist and merges them (earliest
// first_seen, latest last_seen); then clamps last_seen against
// `today_days` (clock rollback never extends the trial), writes BOTH
// files when possible, and returns the effective state. Unwritable
// directories degrade to a process-local first run (logged, never a
// crash). `persistent=false` marks that degradation.
TrialState UpdateTrialState(const std::string& state_dir, const std::string& mirror_dir,
                            std::int64_t today_days, bool* persistent);

enum class LicenseKind {
    kLicensed,         // valid signed license for this device
    kTrial,            // inside the trial window (days_left valid)
    kExpired,          // trial over AND no valid license
    kUnlicensedNoId,   // no device identity AND no valid license
    kBadLicense,       // a license file exists but fails verify (reason set)
};

struct LicenseStatus {
    LicenseKind kind = LicenseKind::kTrial;
    int days_left = 0;          // kTrial only
    std::string reason;         // kBadLicense: why; kUnlicensedNoId detail
    std::string device_code;    // "DJI-..." or "DJI-UNAVAILABLE"
    std::string license_path;   // file that produced the status, if any
    bool trial_persistent = true;  // false = state dir unwritable
};

inline constexpr int kTrialDays = 30;
// Trial warns inside the last 10 days (days 20..29 of 0..29).
inline constexpr int kTrialWarnDays = 10;

// Full evaluation: device hash + trial clock + license file. `root`
// prefixes the /etc and /var paths ("" in production, temp dir in tests —
// same convention as device_id). `today_days` is days since the license
// epoch (util::license::UnixSecondsToLicenseDays).
LicenseStatus CheckLicense(const std::string& config_dir, const std::string& public_key_hex,
                           std::int64_t today_days, const std::string& root);

// Convenience: production paths via DP_TEST_ROOT-aware roots.
LicenseStatus CheckLicenseNow(const std::string& config_dir,
                              const std::string& public_key_hex,
                              std::int64_t now_unix_s);

// Test hook (install tests, benches): when $DP_LICENSE_PUBKEY_HEX is set to a
// 64-char hex key it OVERRIDES the embedded public key. An operator able to
// set the service's environment already controls the host, so this does not
// weaken the binding meaningfully — same class of limitation as deleting the
// trial state file (docs/OPEN-QUESTIONS.md).
std::string EffectivePublicKeyHex(const std::string& embedded_hex);

// Validates `license_text` for THIS device; on success writes it to
// <root>/etc/droppoint/license.key, falling back to
// <config_dir>/license.key when /etc is not writable. Returns the path
// written or an error string.
struct InstallResult {
    bool ok = false;
    std::string path;
    std::string error;
};
InstallResult InstallLicense(const std::string& license_text, const std::string& config_dir,
                             const std::string& public_key_hex, std::int64_t today_days,
                             const std::string& root);

// Cheap re-check for the guidance loop: re-reads the license file only
// when its mtime/size changed; the trial state file is rewritten at most
// once per process day boundary.
class LicenseMonitor {
public:
    LicenseMonitor(std::string config_dir, std::string public_key_hex, std::string root);
    // `now_unix_s` = wall clock. First call does the full CheckLicense;
    // later calls reuse the cached status unless the license file changed.
    const LicenseStatus& Poll(std::int64_t now_unix_s);
    const LicenseStatus& Status() const { return status_; }

private:
    std::string config_dir_;
    std::string pubkey_;
    std::string root_;
    LicenseStatus status_;
    std::string watched_path_;
    std::uint64_t watched_mtime_ns_ = 0;
    std::uintmax_t watched_size_ = 0;
    bool have_watch_ = false;
    bool first_ = true;
    std::int64_t last_day_ = -1;
};

}  // namespace droppoint::hw
