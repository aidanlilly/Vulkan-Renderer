#include "core/Log.hpp"

#include <iostream>
#include <utility>

namespace vr {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::write(LogLevel level, std::string subsystem, std::string message) {
    const auto now = std::chrono::system_clock::now();
    {
        std::lock_guard lock(mutex_);
        entries_.push_back({now, level, subsystem, message});
        if (entries_.size() > 2000) entries_.erase(entries_.begin(), entries_.begin() + 500);
    }
    std::clog << '[' << toString(level) << "] [" << subsystem << "] " << message << '\n';
}

void Logger::info(std::string subsystem, std::string message) { write(LogLevel::Info, std::move(subsystem), std::move(message)); }
void Logger::warning(std::string subsystem, std::string message) { write(LogLevel::Warning, std::move(subsystem), std::move(message)); }
void Logger::error(std::string subsystem, std::string message) { write(LogLevel::Error, std::move(subsystem), std::move(message)); }

std::vector<LogEntry> Logger::snapshot() const {
    std::lock_guard lock(mutex_);
    return entries_;
}

void Logger::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

const char* toString(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace vr
