#include "logging.h"

#include <chrono>
#include <cstdarg>

namespace droppoint::util {

namespace {
const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kError: return "ERROR";
    }
    return "?";
}
}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetLogFilePath(const std::string& path) {
    if (file_) std::fclose(file_);
    file_ = std::fopen(path.c_str(), "a");
}

void Logger::Log(LogLevel level, const std::string& component, const std::string& message) {
    if (level == LogLevel::kDebug && !verbose_) return;

    const auto now = std::chrono::system_clock::now();
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    FILE* sink = file_ ? file_ : (level == LogLevel::kError ? stderr : stdout);
    std::fprintf(sink, "[%lld] [%s] [%s] %s\n", static_cast<long long>(now_us), LevelName(level),
                 component.c_str(), message.c_str());
    std::fflush(sink);
}

}  // namespace droppoint::util
