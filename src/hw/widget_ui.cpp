// hw/widget_ui.cpp
#include "widget_ui.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "widget_map.h"

namespace droppoint::hw {

namespace {

const char* WidgetTypeName(WidgetType t) {
    switch (t) {
        case WidgetType::kButton: return "button";
        case WidgetType::kSwitch: return "switch";
        case WidgetType::kScale: return "scale";
        case WidgetType::kList: return "list";
        case WidgetType::kIntInputBox: return "int_input_box";
    }
    return "button";
}

void JsonEscape(std::ostringstream& os, const std::string& s) {
    for (const char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            default: os << c; break;
        }
    }
}

void EmitRcButtonConfig(std::ostringstream& os, const WidgetDef& w, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    if (!w.rc_mappable) return;
    os << ",\n" << pad << "\"customize_rc_buttons_config\": {\n"
       << pad << "  \"is_enable\": true,\n"
       << pad << "  \"mapping_config_display_order\": " << w.index << "\n"
       << pad << "}";
}

}  // namespace

std::string BuildWidgetConfigJson(const std::vector<std::string>& payload_names) {
    std::ostringstream os;
    os << "{\n"
       << "  \"version\": { \"major\": 1, \"minor\": 0 },\n"
       << "  \"main_interface\": {\n"
       << "    \"floating_window\": { \"is_enable\": true },\n"
       << "    \"speaker\": { \"is_enable_tts\": false, \"is_enable_voice\": false },\n"
       << "    \"widget_list\": [\n";

    bool first = true;
    for (const WidgetDef& w : kWidgetTable) {
        if (!w.main_interface) continue;
        if (!first) os << ",\n";
        first = false;
        os << "      {\n"
           << "        \"widget_index\": " << w.index << ",\n"
           << "        \"widget_type\": \"" << WidgetTypeName(w.type) << "\",\n"
           << "        \"widget_name\": \"";
        JsonEscape(os, w.name);
        os << "\",\n"
           << "        \"icon_file_set\": {\n"
           << "          \"icon_file_name_selected\": \"" << w.icon << "\",\n"
           << "          \"icon_file_name_unselected\": \"" << w.icon << "\"\n"
           << "        }";
        EmitRcButtonConfig(os, w, 8);
        os << "\n      }";
    }

    os << "\n    ]\n"
       << "  },\n"
       << "  \"config_interface\": {\n"
       << "    \"widget_list\": [\n";

    first = true;
    for (const WidgetDef& w : kWidgetTable) {
        if (w.main_interface) continue;
        if (!first) os << ",\n";
        first = false;
        os << "      {\n"
           << "        \"widget_index\": " << w.index << ",\n"
           << "        \"widget_type\": \"" << WidgetTypeName(w.type) << "\",\n"
           << "        \"widget_name\": \"";
        JsonEscape(os, w.name);
        os << "\"";
        if (w.index == kIdxPayload) {
            os << ",\n        \"list_item\": [\n";
            for (std::size_t i = 0; i < payload_names.size(); ++i) {
                os << "          { \"item_name\": \"";
                JsonEscape(os, payload_names[i]);
                os << "\" }" << (i + 1 < payload_names.size() ? ",\n" : "\n");
            }
            os << "        ]";
        } else if (w.index == kIdxSurface) {
            os << ",\n        \"list_item\": [\n";
            for (int i = 0; i < kSurfaceItemCount; ++i) {
                os << "          { \"item_name\": \"" << kSurfaceItems[i] << "\" }"
                   << (i + 1 < kSurfaceItemCount ? ",\n" : "\n");
            }
            os << "        ]";
        } else if (w.index == kIdxTempC) {
            os << ",\n        \"int_input_box_hint\": \"ambient temp, deg C\"";
        }
        EmitRcButtonConfig(os, w, 8);
        os << "\n      }";
    }

    os << "\n    ]\n"
       << "  }\n"
       << "}\n";
    return os.str();
}

void GenerateWidgetDir(const std::string& out_dir, const std::string& icon_dir,
                       const std::vector<std::string>& payload_names) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        throw std::runtime_error("widget dir: cannot create " + out_dir + ": " + ec.message());
    }

    const fs::path config_path = fs::path(out_dir) / "widget_config.json";
    std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("widget dir: cannot write " + config_path.string());
    }
    out << BuildWidgetConfigJson(payload_names);
    out.close();
    if (!out) {
        throw std::runtime_error("widget dir: failed writing " + config_path.string());
    }

    for (const WidgetDef& w : kWidgetTable) {
        if (!w.main_interface || w.icon[0] == '\0') continue;
        const fs::path src = fs::path(icon_dir) / w.icon;
        const fs::path dst = fs::path(out_dir) / w.icon;
        if (!fs::exists(src)) {
            throw std::runtime_error("widget dir: missing icon " + src.string());
        }
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("widget dir: cannot copy " + src.string() + ": " + ec.message());
        }
    }
}

}  // namespace droppoint::hw
