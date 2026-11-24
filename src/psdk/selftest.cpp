// psdk/selftest.cpp — see header. Status: unverified — requires hardware.
#include "selftest.h"

#include <sstream>

#include "app_registration.h"
#include "hal/i_release.h"
#include "hw/conversions.h"
#include "rangefinder_camera_manager.h"
#include "telemetry_subscription.h"
#include "util/version.h"

namespace droppoint::psdk {

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

std::string BuildSelftestReport(const SelftestInput& in, bool* had_fail) {
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

    if (in.credentials != nullptr) {
        Line(os, "OK", "credentials file parsed: " + in.credentials_path);
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

    for (const auto& p : cfg.payloads) {
        if (p.calibrated) {
            Line(os, "OK", "payload '" + p.name + "' calibrated " + p.calibration_date);
        } else {
            Line(os, "WARN", "payload '" + p.name +
                                 "' uncalibrated — ballistic constants are placeholders");
        }
        const double margin = cfg.aircraft.max_payload_kg - p.mass_kg;
        if (margin >= 0.0) {
            Line(os, "OK", "payload '" + p.name + "' weight margin " +
                               std::to_string(margin).substr(0, 5) + " kg under max " +
                               std::to_string(cfg.aircraft.max_payload_kg) + " kg");
        } else {
            Line(os, "FAIL", "payload '" + p.name + "' exceeds max_payload_kg by " +
                                 std::to_string(-margin));
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

    if (in.widget_dir_ok) {
        Line(os, "OK", "widget files generated: " + in.widget_dir);
    } else {
        Line(os, "FAIL", "widget files could not be generated at " + in.widget_dir);
        fail = true;
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

    if (in.telemetry != nullptr) {
        for (const auto& t : in.telemetry->TopicStatus()) {
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
        const auto gnss = const_cast<PsdkTelemetry*>(in.telemetry)->GetGnssStatus();
        Line(os, "INFO", "GNSS: " + std::to_string(gnss.satellite_count) +
                             " satellites, RTK " + RtkName(gnss.rtk_status));
    }
    Line(os, "INFO", "wind topic: absent by design (hover-attitude estimator in app/)");

    if (cfg.aircraft.advanced_features && cfg.aircraft.rangefinder_present) {
        if (in.rangefinder != nullptr && in.rangefinder->IsInitialised()) {
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

}  // namespace droppoint::psdk
