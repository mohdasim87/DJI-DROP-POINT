// hw/selftest_report.cpp — see header.
#include "selftest_report.h"

#include <iomanip>
#include <sstream>

#include "hal/i_release.h"
#include "util/drop_kits.h"
#include "util/version.h"

namespace droppoint::hw {

namespace {

void Line(std::ostringstream& os, const char* tag, const std::string& text) {
    os << '[' << tag << "] " << text << '\n';
}

const char* RtkName(hal::RtkFixStatus s) {
    switch (s) {
        case hal::RtkFixStatus::kFix: return "fixed";
        case hal::RtkFixStatus::kFloat: return "float";
        default: return "none";
    }
}

}  // namespace

std::string BuildSelftestReport(const SelftestFacts& in, bool* had_fail) {
    std::ostringstream os;
    bool fail = false;

    Line(os, "INFO", std::string("droppoint build ") + util::kBuildVersion + " (version " +
                         util::kVersionString + ")");

    if (in.config == nullptr) {
        Line(os, "FAIL", "config files could not be loaded");
        *had_fail = true;
        return os.str();
    }
    const auto& cfg = *in.config;
    Line(os, "OK", "config valid (aircraft=" + cfg.aircraft.model + ", " +
                       std::to_string(cfg.payloads.size()) + " payload profile(s), " +
                       std::to_string(cfg.lenses.size()) + " lens profile(s))");

    if (in.credentials_parsed) {
        if (in.credentials_placeholder) {
            Line(os, "WARN", "credentials file still contains the example placeholders "
                             "— fill in real DJI app credentials at " +
                                 in.credentials_path);
        } else {
            Line(os, "OK", "credentials file parsed: " + in.credentials_path);
        }
    } else if (in.credentials_parse_error) {
        Line(os, "FAIL", "credentials file present but could not be parsed: " +
                             in.credentials_path);
        fail = true;
    } else {
        Line(os, "FAIL", "no credentials file found (searched --credentials, " +
                             in.credentials_path + ")");
        fail = true;
    }

    Line(os, "INFO", "release backend: " + cfg.aircraft.release.description);

    if (!cfg.aircraft.release.kit.empty()) {
        if (const util::DropKit* kit = util::FindDropKit(cfg.aircraft.release.kit)) {
            std::ostringstream kit_ss;
            kit_ss << "drop kit: " << kit->maker << " " << kit->model << " ("
                   << kit->hooks << " hook(s), " << std::fixed << std::setprecision(3)
                   << kit->weight_kg << " kg, connects to " << kit->interface << ")";
            Line(os, "INFO", kit_ss.str());
            if (kit->weight_kg > 0.0 &&
                cfg.aircraft.onboard_equipment_kg < kit->weight_kg) {
                Line(os, "WARN",
                     "onboard_equipment_kg is less than the drop kit alone weighs — "
                     "weigh your build");
            }
        }
    } else if (cfg.aircraft.release.backend != util::config::ReleaseBackend::kPwmServo) {
        Line(os, "WARN",
             "no release.kit set — set release.kit in aircraft.yaml so the kit is "
             "checked against this aircraft (see docs/DROP-KITS.md)");
    }

    for (const auto& p : cfg.payloads) {
        if (p.calibrated) {
            Line(os, "OK", "payload '" + p.name + "' calibrated " + p.calibration_date);
        } else {
            Line(os, "WARN", "payload '" + p.name +
                                 "' uncalibrated — ballistic constants are placeholders");
        }
        // max_payload_kg is the budget for EVERYTHING hanging off the aircraft
        // — onboard equipment (companion computer, mounts, drop kit) plus the
        // releasable payload itself.
        const double margin =
            cfg.aircraft.max_payload_kg - cfg.aircraft.onboard_equipment_kg - p.mass_kg;
        std::ostringstream margin_ss;
        margin_ss << std::fixed << std::setprecision(3) << margin;
        if (margin >= 0.0) {
            std::ostringstream max_ss;
            max_ss << std::fixed << std::setprecision(3) << cfg.aircraft.max_payload_kg;
            Line(os, "OK", "payload '" + p.name + "' weight margin " + margin_ss.str() +
                               " kg under max " + max_ss.str() + " kg");
        } else {
            Line(os, "FAIL", "payload '" + p.name + "' exceeds the releasable budget by " +
                                 margin_ss.str().substr(1) + " kg");
            fail = true;
        }
    }
    if (cfg.aircraft.airframe_calibrated) {
        Line(os, "OK", "airframe calibrated (cda_airframe " +
                           std::to_string(cfg.aircraft.cda_airframe) + ")");
    } else {
        Line(os, "WARN", "airframe uncalibrated — cda_airframe is a placeholder");
    }

    Line(os, "INFO", "DEM tiles found in " + cfg.system.dem_directory + ": " +
                         std::to_string(in.dem_tile_count));
    if (in.home_tie) {
        Line(os, "OK", "terrain home tie recorded at " + std::to_string(in.home_tie_alt_m) +
                           " m (ellipsoid)");
    } else if (in.link) {
        Line(os, "WARN", "no terrain home tie yet (recorded while the aircraft is on the "
                         "ground before takeoff)");
    }
    Line(os, "INFO", "PPS time synchronisation: not implemented in this build");

    // The DJI SDK Certified Chip — required before PSDK works on the
    // Matrice 4 series; on M400 builds it is inside DJI's cable path.
    if (cfg.aircraft.auth_chip.enabled) {
        Line(os, "INFO", "SDK certified chip: enabled on " +
                             cfg.aircraft.auth_chip.i2c_device + ", reset " +
                             cfg.aircraft.auth_chip.reset_gpio_chip + " line " +
                             std::to_string(cfg.aircraft.auth_chip.reset_gpio_line));
        if (!in.auth_chip_i2c_node_present || !in.auth_chip_gpio_node_present) {
            std::string missing;
            if (!in.auth_chip_i2c_node_present) {
                missing = cfg.aircraft.auth_chip.i2c_device;
            }
            if (!in.auth_chip_gpio_node_present) {
                if (!missing.empty()) missing += " and ";
                missing += cfg.aircraft.auth_chip.reset_gpio_chip;
            }
            if (in.link) {
                Line(os, "FAIL", "SDK certified chip device nodes missing: " + missing +
                                     " — check the wiring (docs/SETUP-MANUAL.md 5.3)");
                fail = true;
            } else {
                Line(os, "WARN", "SDK certified chip device nodes not present: " + missing +
                                     " (expected on the aircraft, not on this machine)");
            }
        }
    } else {
        Line(os, "INFO", "SDK certified chip: not required for this build");
    }

    if (in.widget_dir_ok) {
        Line(os, "OK", "widget files generated: " + in.widget_dir);
    } else {
        Line(os, "FAIL", "widget files could not be generated at " + in.widget_dir);
        fail = true;
    }

    // License: arming is refused when expired, so with the aircraft link
    // it is a FAIL; under --no-link it is a WARN (the bench machine may be
    // a fresh checkout with no state dir).
    if (!in.license_text.empty()) {
        if (in.license_expired) {
            if (in.link) {
                Line(os, "FAIL", "license expired — install a vendor key "
                                 "(droppoint --license-install, docs/SETUP-MANUAL.md)");
                fail = true;
            } else {
                Line(os, "WARN", "license expired: " + in.license_text);
            }
        } else {
            Line(os, "INFO", "license: " + in.license_text);
        }
    }

    // ---- checks that need the aircraft link --------------------------------
    if (!in.link) {
        Line(os, "SKIP", "PSDK link not established (--no-link): aircraft model match, "
                         "credential acceptance, telemetry subscriptions, rangefinder and "
                         "live home-tie checks skipped");
        *had_fail = fail;
        return os.str();
    }

    if (in.model_matches) {
        Line(os, "OK", "aircraft model match: FC reports " + in.aircraft_reported +
                           ", config expects " + cfg.aircraft.model);
    } else {
        Line(os, "FAIL", "aircraft model mismatch: FC reports " + in.aircraft_reported +
                             ", config expects " + cfg.aircraft.model);
        fail = true;
    }
    Line(os, "OK", "PSDK link established and credentials accepted by the FC");

    if (in.telemetry_present) {
        for (const auto& t : in.topics) {
            if (t.subscribed) {
                Line(os, "OK", "subscription topic " + t.name + " acquired @" +
                                   std::to_string(t.frequency_hz) + " Hz");
            } else if (t.required) {
                Line(os, "FAIL", "required subscription topic " + t.name + " MISSING");
                fail = true;
            } else {
                Line(os, "WARN", "optional subscription topic " + t.name + " not acquired");
            }
        }
        Line(os, "INFO", "GNSS: " + std::to_string(in.gnss.satellite_count) +
                             " satellites, RTK " + RtkName(in.gnss.rtk_status));
    }
    Line(os, "INFO", "wind topic: absent by design (hover-attitude estimator in app/)");

    if (cfg.aircraft.advanced_features && cfg.aircraft.rangefinder_present) {
        if (in.rangefinder_initialised) {
            Line(os, "OK", "laser rangefinder initialised (camera manager)");
        } else {
            Line(os, "WARN", "laser rangefinder configured but camera manager unavailable");
        }
    } else {
        Line(os, "INFO", "laser rangefinder: not configured for this aircraft");
    }

    if (in.release != nullptr) {
        if (in.release->IsResponding()) {
            Line(os, "OK", "release backend responding");
        } else {
            Line(os, "FAIL", "release backend NOT responding");
            fail = true;
        }
    }

    *had_fail = fail;
    return os.str();
}

}  // namespace droppoint::hw
