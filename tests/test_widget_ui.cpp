// tests/test_widget_ui.cpp
//
// The generated widget directory: widget_config.json must parse (yaml-cpp
// reads JSON), carry the 10 widgets from hw/widget_map.h with unique
// indices, the payload list from the loaded config, and every name within
// the 32-char widget limit. Icons referenced by main_interface widgets must
// be copied into the output dir.
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "test_framework.h"

#include "hw/widget_map.h"
#include "hw/widget_ui.h"

namespace {

std::filesystem::path FreshTempDir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST_CASE("WidgetUi: generated JSON parses and carries all 10 widgets") {
    using droppoint::hw::kWidgetTable;
    const std::vector<std::string> payloads = {"marker_light_40g", "med_pouch_80g"};
    const std::string json = droppoint::hw::BuildWidgetConfigJson(payloads);
    const YAML::Node root = YAML::Load(json);

    REQUIRE(root["main_interface"]["floating_window"]["is_enable"].as<bool>());
    REQUIRE(!root["main_interface"]["speaker"]["is_enable_tts"].as<bool>());
    REQUIRE(!root["main_interface"]["speaker"]["is_enable_voice"].as<bool>());

    std::set<int> indices;
    std::set<std::string> types;
    for (const auto& section : {"main_interface", "config_interface"}) {
        for (const auto& w : root[section]["widget_list"]) {
            const int idx = w["widget_index"].as<int>();
            indices.insert(idx);
            types.insert(w["widget_type"].as<std::string>());
            REQUIRE(w["widget_name"].as<std::string>().size() <= 32);
        }
    }
    REQUIRE(indices.size() == 10);
    for (const auto& def : kWidgetTable) {
        REQUIRE(indices.count(def.index) == 1);
    }
    REQUIRE(types.count("button") == 1);
    REQUIRE(types.count("switch") == 1);
    REQUIRE(types.count("list") == 1);
    REQUIRE(types.count("int_input_box") == 1);
}

TEST_CASE("WidgetUi: payload and surface list items come from config/table") {
    const std::vector<std::string> payloads = {"aaa", "bbb", "ccc"};
    const YAML::Node root =
        YAML::Load(droppoint::hw::BuildWidgetConfigJson(payloads));

    const YAML::Node list = root["config_interface"]["widget_list"];
    int payload_pos = -1;
    int surface_pos = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const int idx = list[i]["widget_index"].as<int>();
        if (idx == droppoint::hw::kIdxPayload) payload_pos = static_cast<int>(i);
        if (idx == droppoint::hw::kIdxSurface) surface_pos = static_cast<int>(i);
    }
    REQUIRE(payload_pos >= 0);
    REQUIRE(surface_pos >= 0);
    const YAML::Node payload_widget = list[static_cast<std::size_t>(payload_pos)];
    const YAML::Node surface_widget = list[static_cast<std::size_t>(surface_pos)];
    REQUIRE(payload_widget["list_item"].size() == 3);
    REQUIRE(payload_widget["list_item"][1]["item_name"].as<std::string>() == "bbb");
    REQUIRE(surface_widget["list_item"].size() == droppoint::hw::kSurfaceItemCount);
    REQUIRE(surface_widget["list_item"][0]["item_name"].as<std::string>() ==
            "OPEN WATER / DESERT");
}

TEST_CASE("WidgetUi: RC-button mapping enabled for widgets 0-6") {
    const YAML::Node root = YAML::Load(droppoint::hw::BuildWidgetConfigJson({"a"}));
    int mapped = 0;
    for (const auto& section : {"main_interface", "config_interface"}) {
        for (const auto& w : root[section]["widget_list"]) {
            const int idx = w["widget_index"].as<int>();
            const bool has_rc =
                w["customize_rc_buttons_config"] &&
                w["customize_rc_buttons_config"]["is_enable"].as<bool>();
            if (has_rc) {
                ++mapped;
                REQUIRE(idx <= 6);
            }
        }
    }
    REQUIRE(mapped == 7);
}

TEST_CASE("WidgetUi: GenerateWidgetDir writes config and copies icons") {
    const auto dir = FreshTempDir("droppoint_widget_gen");
    droppoint::hw::GenerateWidgetDir(dir.string(), DROPPOINT_WIDGET_ASSET_DIR,
                                     {"payload_a", "payload_b"});

    REQUIRE(std::filesystem::exists(dir / "widget_config.json"));
    // Every icon referenced by a main_interface widget exists in the dir.
    const YAML::Node root =
        YAML::LoadFile((dir / "widget_config.json").string());
    for (const auto& w : root["main_interface"]["widget_list"]) {
        const std::string icon =
            w["icon_file_set"]["icon_file_name_unselected"].as<std::string>();
        REQUIRE(std::filesystem::exists(dir / icon));
        REQUIRE(std::filesystem::file_size(dir / icon) > 0);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("WidgetUi: widget names fit the 32-char limit") {
    for (const auto& def : droppoint::hw::kWidgetTable) {
        REQUIRE(std::string(def.name).size() <= 32);
    }
}
