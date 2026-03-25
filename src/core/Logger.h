#pragma once
#include <format>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

namespace Unsuffered {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static void Init(const std::string& logfile = "unsuffered.log") {
        Instance().m_file.open(logfile, std::ios::out | std::ios::trunc);
    }

    template<typename... Args>
    static void Log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::floor<std::chrono::seconds>(now);
        const char* prefix[] = {"[DBG]", "[INF]", "[WRN]", "[ERR]"};
        auto full = std::format("{} {} {}", prefix[static_cast<int>(level)],
                                std::format("{:%H:%M:%S}", time), msg);
        std::cout << full << "\n";
        if (Instance().m_file.is_open()) {
            Instance().m_file << full << "\n";
            Instance().m_file.flush();
        }
    }

    template<typename... Args>
    static void Debug(std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void Info(std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void Warn(std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void Error(std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

private:
    std::ofstream m_file;
    static Logger& Instance() { static Logger l; return l; }
};

} // namespace Unsuffered
