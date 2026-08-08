// util/logging.h
//
// Minimal structured logging with levels and a --verbose flag (spec 13).
// Deliberately dependency-free so it works identically on the companion
// computer and on a dev machine with no network access.
#pragma once

#include <cstdio>
#include <string>

namespace droppoint::util {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

class Logger {
public:
    static Logger& Instance();

    void SetVerbose(bool verbose) { verbose_ = verbose; }
    void SetLogFilePath(const std::string& path);

    void Log(LogLevel level, const std::string& component, const std::string& message);

private:
    Logger() = default;
    bool verbose_ = false;
    FILE* file_ = nullptr;
};

}  // namespace droppoint::util

#define DP_LOG_DEBUG(component, msg) droppoint::util::Logger::Instance().Log(droppoint::util::LogLevel::kDebug, component, msg)
#define DP_LOG_INFO(component, msg) droppoint::util::Logger::Instance().Log(droppoint::util::LogLevel::kInfo, component, msg)
#define DP_LOG_WARN(component, msg) droppoint::util::Logger::Instance().Log(droppoint::util::LogLevel::kWarn, component, msg)
#define DP_LOG_ERROR(component, msg) droppoint::util::Logger::Instance().Log(droppoint::util::LogLevel::kError, component, msg)
