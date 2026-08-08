// tools/lenscal/main.cpp
//
// Spec section 11 calls for "OpenCV checkerboard intrinsics and distortion,
// writing lenses.yaml." OpenCV is a large dependency this repository does
// not vendor (see docs/OPEN-QUESTIONS.md for the tradeoff record); this
// tool currently implements the manual-entry path only: it takes
// already-measured intrinsics (from a datasheet as an uncalibrated
// placeholder, or from an external checkerboard calibration run using
// standalone OpenCV/MATLAB tooling) and writes a schema-correct
// lenses.yaml entry, clearly marked calibrated or uncalibrated per spec 6.7
// ("never present datasheet FOV as measured truth").
//
// The documented OpenCV checkerboard procedure itself lives in
// docs/CALIBRATION.md; this tool is the "writing lenses.yaml" half of that
// spec requirement, decoupled from the OpenCV dependency question.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace {
void PrintUsage() {
    std::printf(
        "usage: droppoint_lenscal --aircraft <model> --source <stream_source> --width <px> --height <px>\n"
        "                          --hfov-deg <deg> --vfov-deg <deg> [--cx <px>] [--cy <px>]\n"
        "                          [--k1 <v>] [--k2 <v>] [--k3 <v>] [--p1 <v>] [--p2 <v>]\n"
        "                          [--calibrated <YYYY-MM-DD>] --out <lenses.yaml fragment path>\n"
        "\n"
        "Writes one lens profile entry. Append the output to config/lenses.yaml's\n"
        "`lenses:` list. Omitting --calibrated writes calibration_date: \"\" (uncalibrated\n"
        "placeholder), per spec section 10.\n"
        "\n"
        "For automatic checkerboard-based intrinsics/distortion extraction, run an\n"
        "external OpenCV calibration script (see docs/CALIBRATION.md) and feed its\n"
        "results into this tool's flags -- OpenCV is intentionally not vendored here.\n");
}
}  // namespace

int main(int argc, char** argv) {
    std::string aircraft, source, out_path, calibrated_date;
    double width = 1920, height = 1080, hfov = 0, vfov = 0, cx = -1, cy = -1;
    double k1 = 0, k2 = 0, k3 = 0, p1 = 0, p2 = 0;
    bool has_distortion = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--aircraft") aircraft = next();
        else if (arg == "--source") source = next();
        else if (arg == "--width") width = std::stod(next());
        else if (arg == "--height") height = std::stod(next());
        else if (arg == "--hfov-deg") hfov = std::stod(next());
        else if (arg == "--vfov-deg") vfov = std::stod(next());
        else if (arg == "--cx") cx = std::stod(next());
        else if (arg == "--cy") cy = std::stod(next());
        else if (arg == "--k1") { k1 = std::stod(next()); has_distortion = true; }
        else if (arg == "--k2") { k2 = std::stod(next()); has_distortion = true; }
        else if (arg == "--k3") { k3 = std::stod(next()); has_distortion = true; }
        else if (arg == "--p1") { p1 = std::stod(next()); has_distortion = true; }
        else if (arg == "--p2") { p2 = std::stod(next()); has_distortion = true; }
        else if (arg == "--calibrated") calibrated_date = next();
        else if (arg == "--out") out_path = next();
        else if (arg == "--help" || arg == "-h") { PrintUsage(); return 0; }
    }

    if (aircraft.empty() || source.empty() || hfov <= 0 || vfov <= 0 || out_path.empty()) {
        PrintUsage();
        return 1;
    }
    if (cx < 0) cx = width / 2.0;
    if (cy < 0) cy = height / 2.0;

    std::ofstream out(out_path);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 1;
    }

    out << "  - aircraft_model: \"" << aircraft << "\"\n"
        << "    stream_source: \"" << source << "\"\n"
        << "    camera_index: 0\n"
        << "    image_width_px: " << width << "\n"
        << "    image_height_px: " << height << "\n"
        << "    hfov_deg: " << hfov << "\n"
        << "    vfov_deg: " << vfov << "\n"
        << "    principal_point_x_px: " << cx << "\n"
        << "    principal_point_y_px: " << cy << "\n";
    if (has_distortion) {
        out << "    k1: " << k1 << "\n    k2: " << k2 << "\n    k3: " << k3 << "\n    p1: " << p1
            << "\n    p2: " << p2 << "\n";
    }
    out << "    calibration_date: \"" << calibrated_date << "\"  # empty = uncalibrated placeholder\n";
    out.close();

    std::printf("Wrote lens profile fragment to %s. Append it under lenses.yaml's `lenses:` list.\n",
                out_path.c_str());
    if (calibrated_date.empty()) {
        std::printf("WARNING: no --calibrated date given; this entry is an UNCALIBRATED placeholder.\n");
    }
    return 0;
}
