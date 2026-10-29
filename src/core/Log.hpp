#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace vr {

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
    std::chrono::system_clock::time_point time;
    LogLevel level{};
    std::string subsystem;
    std::string message;
};

class Logger {
public:
    static Logger& instance();
    void write(LogLevel level, std::string subsystem, std::string message);
    void info(std::string subsystem, std::string message);
    void warning(std::string subsystem, std::string message);
    void error(std::string subsystem, std::string message);
    [[nodiscard]] std::vector<LogEntry> snapshot() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
};

[[nodiscard]] const char* toString(LogLevel level);

} // namespace vr
