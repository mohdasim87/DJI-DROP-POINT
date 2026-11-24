// util/drop_kits.cpp — see header. The figures are each maker's published
// specification (docs/DROP-KITS.md); none has been measured by us.
#include "drop_kits.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace droppoint::util {

namespace {

using config::ReleaseBackend;

const char* BackendName(ReleaseBackend b) {
    switch (b) {
        case ReleaseBackend::kPwmServo: return "pwm_servo";
        case ReleaseBackend::kExternalOperator: return "external_operator";
        case ReleaseBackend::kPsdkWidget: return "psdk_widget";
    }
    return "?";
}

std::string Join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += sep;
        s += v[i];
    }
    return s;
}

std::string BackendList(const DropKit& k) {
    std::string s;
    for (size_t i = 0; i < k.backends.size(); ++i) {
        if (i) s += ", ";
        s += BackendName(k.backends[i]);
    }
    return s;
}

}  // namespace

const std::vector<DropKit>& DropKitCatalogue() {
    static const std::vector<DropKit> kits = {
        // ---- DJI Matrice 4 series (200 g total lift budget) ----------------
        {"czi_th2", "CZI", "TH2 Throwing Hook",
         {"M4T", "M4E"},
         "none (light sensor; power from battery USB-C)",
         0.070, 2, 0.5,
         "aircraft bottom auxiliary light in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator},
         "set the bottom auxiliary light to Off, not Auto, or the aircraft "
         "can drop by itself in low light"},
        {"czi_ft4p", "CZI (Cytop)", "FT4-P Dual-Channel Drop System",
         {"M4T", "M4E"},
         "USB-C (PSDK)",
         0.050, 2, 2.0,
         "kit button in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator},
         "maker does not state which aircraft port it uses — ask before "
         "buying (the main E-Port may be needed for the companion computer)"},
        {"jz_ad2", "JZ", "AD2 Two Drop Kit",
         {"M4T", "M4E", "M4D", "M4TD"},
         "E-Port Lite",
         0.043, 2, 1.0,
         "kit button in DJI Pilot 2 or RC button",
         {ReleaseBackend::kExternalOperator},
         "lightest M4-series kit; E-Port Lite leaves the main E-Port for "
         "DropPoint's computer"},

        // ---- DJI Matrice 400 / M300-class ----------------------------------
        {"czi_th4", "CZI", "TH4 V3 Throwing Hook Drop System",
         {"M400", "M350_RTK", "M300_RTK"},
         "SkyPort V2",
         0.320, 4, 10.0,
         "kit button in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator, ReleaseBackend::kPsdkWidget},
         "M400 support needs the kit's newer firmware — check it when it "
         "arrives"},
        {"jz_pts4", "JZ", "PTS4 Visual Drop Kit",
         {"M400", "M350_RTK", "M300_RTK"},
         "SkyPort V2",
         0.210, 4, 10.0,
         "kit button in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator, ReleaseBackend::kPsdkWidget},
         "has an aiming camera"},
        {"jz_ut4a", "JZ", "UT4A Utility Drop Kit",
         {"M400", "M350_RTK"},
         "JZ T-Port (JZ bracket)",
         0.178, 4, 6.0,
         "kit button in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator, ReleaseBackend::kPsdkWidget},
         "needs JZ's Matrice 400 bracket; includes siren and strobe"},
        {"jz_pl10", "JZ", "PL10 Lifting Kit",
         {"M400"},
         "E-Port V2",
         0.080, 1, 10.0,
         "kit button in DJI Pilot 2 or RC button",
         {ReleaseBackend::kExternalOperator, ReleaseBackend::kPsdkWidget},
         "single lifting hook"},
        {"dromight_talon", "DroMight", "Talon Drop System for Matrice 400",
         {"M400"},
         "E-Port V2",
         0.482, 1, 0.0,
         "kit button in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator, ReleaseBackend::kPsdkWidget},
         "maker gives no per-hook rating — the aircraft's 6 kg limit applies"},
        {"cybowindow_4hook", "Cybowindow", "M400 4-hook Drop Kit",
         {"M400"},
         "E-Port V2",
         0.645, 4, 6.0,
         "kit button in DJI Pilot 2",
         {ReleaseBackend::kExternalOperator, ReleaseBackend::kPsdkWidget},
         "heaviest M400 kit — 645 g of the payload budget"},

        // ---- build your own -------------------------------------------------
        {"custom_pwm_servo", "(own build)",
         "Servo latch on a companion-computer PWM pin",
         config::KnownAircraftModels(),
         "PWM pin",
         0.0, 1, 0.0,
         "DropPoint",
         {ReleaseBackend::kPwmServo},
         "the only option DropPoint actuates itself — AUTO release possible"},
    };
    return kits;
}

const DropKit* FindDropKit(const std::string& id) {
    for (const auto& k : DropKitCatalogue()) {
        if (k.id == id) return &k;
    }
    return nullptr;
}

std::vector<const DropKit*> KitsForModel(const std::string& model) {
    std::vector<const DropKit*> out;
    for (const auto& k : DropKitCatalogue()) {
        if (std::find(k.aircraft_models.begin(), k.aircraft_models.end(), model) !=
            k.aircraft_models.end()) {
            out.push_back(&k);
        }
    }
    return out;
}

std::string FormatKitTable(const std::string& model_filter) {
    const auto& kits = DropKitCatalogue();
    const bool mark = !model_filter.empty();
    auto fits = [&](const DropKit& k) {
        return std::find(k.aircraft_models.begin(), k.aircraft_models.end(),
                         model_filter) != k.aircraft_models.end();
    };

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> header;
    if (mark) header.push_back("");
    header.insert(header.end(),
                  {"id", "maker", "model", "fits aircraft", "connects to",
                   "kg", "hooks", "kg/hook", "triggered by", "backends"});
    rows.push_back(header);
    for (const auto& k : kits) {
        std::vector<std::string> r;
        if (mark) r.push_back(fits(k) ? "*" : "");
        std::ostringstream w;
        w << std::fixed << std::setprecision(3) << k.weight_kg;
        const std::string fits_str =
            k.aircraft_models.size() == config::KnownAircraftModels().size()
                ? "all known models"
                : Join(k.aircraft_models, ", ");
        r.insert(r.end(),
                 {k.id, k.maker, k.model, fits_str,
                  k.interface, w.str(), std::to_string(k.hooks),
                  k.max_load_per_hook_kg > 0.0
                      ? [&] {
                            std::ostringstream s;
                            s << std::fixed << std::setprecision(1)
                              << k.max_load_per_hook_kg;
                            return s.str();
                        }()
                      : "-",
                  k.trigger, BackendList(k)});
        rows.push_back(std::move(r));
    }

    std::vector<size_t> widths(rows.front().size(), 0);
    for (const auto& r : rows) {
        for (size_t c = 0; c < r.size(); ++c) {
            widths[c] = std::max(widths[c], r[c].size());
        }
    }

    std::ostringstream os;
    os << "Compatible drop kits (release.kit values — see docs/DROP-KITS.md)\n";
    if (mark) {
        os << "kits marked * fit aircraft model '" << model_filter << "'\n";
    }
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        if (ri == 1) {
            for (size_t c = 0; c < widths.size(); ++c) {
                os << std::string(widths[c] + 2, '-');
            }
            os << '\n';
        }
        for (size_t c = 0; c < rows[ri].size(); ++c) {
            os << std::left << std::setw(static_cast<int>(widths[c]) + 2)
               << rows[ri][c];
        }
        os << '\n';
    }
    return os.str();
}

}  // namespace droppoint::util
