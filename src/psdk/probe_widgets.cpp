// psdk/probe_widgets.cpp — see header. Status: unverified — requires hardware.
#include "probe_widgets.h"

#include <dji_typedef.h>
#include <dji_widget_manager.h>

#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace droppoint::psdk {

namespace {

struct DownloadState {
    std::mutex mu;
    std::map<std::uint32_t, std::string> names;      // fileIndex -> fileName
    std::map<std::uint32_t, float> progress;         // fileIndex -> percent
    std::string out_dir;
};
DownloadState g_dl;

T_DjiReturnCode OnDownloadData(T_DjiDownloadWidgetFileInfo packetInfo, const uint8_t* data,
                              uint16_t dataLen) {
    std::lock_guard<std::mutex> lock(g_dl.mu);
    const auto it = g_dl.names.find(packetInfo.fileIndex);
    if (it == g_dl.names.end()) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    const std::filesystem::path path =
        std::filesystem::path(g_dl.out_dir) / it->second;
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (f) {
        f.write(reinterpret_cast<const char*>(data), dataLen);
    }
    g_dl.progress[packetInfo.fileIndex] = packetInfo.progressInPercent;
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

void OnWidgetStates(E_DjiMountPosition position, T_DjiWidgetStates* statesData,
                    uint8_t widgetNum) {
    for (uint8_t i = 0; i < widgetNum; ++i) {
        std::printf("  state change: position=%d index=%u type=%d value=%d\n",
                    static_cast<int>(position), statesData[i].widgetIndex,
                    static_cast<int>(statesData[i].widgetType), statesData[i].widgetValue);
    }
}

void PrintWidgetList(const std::filesystem::path& config_path) {
    YAML::Node root = YAML::LoadFile(config_path.string());
    for (const char* section : {"main_interface", "config_interface"}) {
        const YAML::Node iface = root[section];
        if (!iface) continue;
        const YAML::Node list = iface["widget_list"];
        if (!list) continue;
        for (const auto& w : list) {
            const int index = w["widget_index"].as<int>(-1);
            const std::string type = w["widget_type"].as<std::string>("?");
            const std::string name = w["widget_name"].as<std::string>("?");
            std::printf("  widget index=%d type=%s name=\"%s\"   <- release.widget candidates\n",
                        index, type.c_str(), name.c_str());
        }
    }
}

}  // namespace

int ProbeWidgets(int mount_position, const std::string& out_dir) {
    const auto position = static_cast<E_DjiMountPosition>(mount_position);
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);

    T_DjiReturnCode ret = DjiWidgetManager_Init();
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("DjiWidgetManager_Init failed — Widget Manager is M400 only");
    }

    ret = DjiWidgetManager_RegDownloadFileDataCallback(position, OnDownloadData);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("RegDownloadFileDataCallback failed");
    }

    T_DjiWidgetManagerFileList fileList{};
    ret = DjiWidgetManager_WidgetDownloadFileList(position, &fileList);
    if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        throw std::runtime_error("WidgetDownloadFileList failed — is a payload mounted at position " +
                                 std::to_string(mount_position) + "?");
    }
    std::printf("probe: %u widget file(s) on mount position %d\n", fileList.totalCount,
                mount_position);

    {
        std::lock_guard<std::mutex> lock(g_dl.mu);
        g_dl.out_dir = out_dir;
        for (uint8_t i = 0; i < fileList.totalCount; ++i) {
            const auto& info = fileList.fileListInfo[i];
            g_dl.names[info.fileIndex] = info.fileName;
            std::printf("  file %u: %s (%u bytes)\n", info.fileIndex, info.fileName,
                        info.fileSize);
        }
    }

    for (uint8_t i = 0; i < fileList.totalCount; ++i) {
        ret = DjiWidgetManager_DownloadFileByIndex(position, fileList.fileListInfo[i].fileIndex);
        if (ret != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            std::printf("  download of file %u failed\n", fileList.fileListInfo[i].fileIndex);
        }
    }

    // Wait for the transfers: done when every file reports >= 100%.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(g_dl.mu);
            bool done = !g_dl.names.empty();
            for (const auto& [idx, name] : g_dl.names) {
                const auto it = g_dl.progress.find(idx);
                if (it == g_dl.progress.end() || it->second < 100.0f) done = false;
            }
            if (done) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    const std::filesystem::path config_path =
        std::filesystem::path(out_dir) / "widget_config.json";
    if (std::filesystem::exists(config_path)) {
        std::printf("probe: widget_config.json contents:\n");
        try {
            PrintWidgetList(config_path);
        } catch (const std::exception& e) {
            std::printf("  (could not parse widget_config.json: %s)\n", e.what());
        }
    } else {
        std::printf("probe: no widget_config.json downloaded — check the file list above\n");
    }

    std::printf("probe: listening to widget state changes for 15 s — press the kit's widgets now\n");
    ret = DjiWidgetManager_SubscribePayloadWidgetStates(position, OnWidgetStates);
    if (ret == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        DjiWidgetManager_UnsubscribePayloadWidgetStates(position);
    } else {
        std::printf("probe: SubscribePayloadWidgetStates failed\n");
    }

    DjiWidgetManager_UnRegDownloadFileDataCallback(position);
    std::printf("probe: done. Put the drop widget's index/type into release.widget in "
                "aircraft.yaml.\n");
    return 0;
}

}  // namespace droppoint::psdk
