#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>

namespace Quasar {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

class Logger {
private:
    static std::unique_ptr<Logger> instance;
    static std::mutex instanceMutex;
    
    std::ofstream logFile;
    std::mutex logMutex;
    LogLevel currentLevel;
    bool consoleOutput;
    bool fileOutput;
    std::string logFilePath;

    Logger();

    std::string get_current_timestamp() const;
    std::string get_level_string(LogLevel level) const;
    std::string get_color_code(LogLevel level) const;
    void write_log(LogLevel level, const std::string& message, const std::string& file, int line);

    // Helper for formatted logging
    void format_impl(std::stringstream& ss, const std::string& format);

    template<typename T, typename... Args>
    void format_impl(std::stringstream& ss, const std::string& format, T&& t, Args... args) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            ss << format.substr(0, pos) << std::forward<T>(t);
            format_impl(ss, format.substr(pos + 2), args...);
        } else {
            ss << format;
        }
    }

public:
    static Logger& get_instance();
    ~Logger();

    // Delete copy constructor and assignment operator
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_log_level(LogLevel level);
    LogLevel get_log_level() const;
    void enable_console_cutput(bool enable);
    bool init_file_output(const std::string& filepath);
    void disable_fileOutput();

    void trace(const std::string& message, const std::string& file = __FILE__, int line = __LINE__);
    void debug(const std::string& message, const std::string& file = __FILE__, int line = __LINE__);
    void info(const std::string& message, const std::string& file = __FILE__, int line = __LINE__);
    void warn(const std::string& message, const std::string& file = __FILE__, int line = __LINE__);
    void error(const std::string& message, const std::string& file = __FILE__, int line = __LINE__);
    void fatal(const std::string& message, const std::string& file = __FILE__, int line = __LINE__);

    // Template method for formatted logging
    template<typename... Args>
    void log(LogLevel level, const std::string& format, Args... args) {
        if (level < currentLevel) return;
        
        std::stringstream ss;
        format_impl(ss, format, args...);
        write_log(level, ss.str(), __FILE__, __LINE__);
    }
};

} // namespace Quasar

// Conditional macros based on build mode
#ifndef NDEBUG
    // Debug build - all macros work
    #define LOG_TRACE(msg) Quasar::Logger::get_instance().trace(msg, __FILE__, __LINE__)
    #define LOG_DEBUG(msg) Quasar::Logger::get_instance().debug(msg, __FILE__, __LINE__)
    #define LOG_INFO(msg)  Quasar::Logger::get_instance().info(msg, __FILE__, __LINE__)
    #define LOG_WARN(msg)  Quasar::Logger::get_instance().warn(msg, __FILE__, __LINE__)
    #define LOG_ERROR(msg) Quasar::Logger::get_instance().error(msg, __FILE__, __LINE__)
    #define LOG_FATAL(msg) Quasar::Logger::get_instance().fatal(msg, __FILE__, __LINE__)
#else
    // Release build - only warn, error, and fatal work
    #define LOG_TRACE(msg) ((void)0)
    #define LOG_DEBUG(msg) ((void)0)
    #define LOG_INFO(msg)  ((void)0)
    #define LOG_WARN(msg)  Quasar::Logger::get_instance().warn(msg, __FILE__, __LINE__)
    #define LOG_ERROR(msg) Quasar::Logger::get_instance().error(msg, __FILE__, __LINE__)
    #define LOG_FATAL(msg) Quasar::Logger::get_instance().fatal(msg, __FILE__, __LINE__)
#endif