 #pragma once

#include "Defines.h"
#include <chrono>

namespace Quasar
{
    class QS_API Log {
        public:
        typedef enum log_level {
            LOG_LEVEL_FATAL = 0,
            LOG_LEVEL_ERROR = 1,
            LOG_LEVEL_WARN = 2,
            LOG_LEVEL_INFO = 3,
            LOG_LEVEL_DEBUG = 4,
            LOG_LEVEL_TRACE = 5
        } log_level;

        static b8 init();
        static void shutdown();
        static void core_log_output(log_level level, const char* msg, ...);
        static Log& get_instance() {return instance;} 

        // Note: Editor Console starts at runtime, this is used for activating editor logging
        static b8 is_runtime;

        private:
        static Log instance;
        static std::chrono::high_resolution_clock::time_point engine_start_time;
        static std::chrono::high_resolution_clock::time_point current_time;
    };
} // namespace Quasar

#define LOG_FATAL(msg, ...) Log::get_instance().core_log_output(Log::LOG_LEVEL_FATAL, msg, ##__VA_ARGS__);
#define LOG_ERROR(msg, ...) Log::get_instance().core_log_output(Log::LOG_LEVEL_ERROR, msg, ##__VA_ARGS__);
#define LOG_WARN(msg, ...) Log::get_instance().core_log_output(Log::LOG_LEVEL_WARN, msg, ##__VA_ARGS__);
#define LOG_INFO(msg, ...) Log::get_instance().core_log_output(Log::LOG_LEVEL_INFO, msg, ##__VA_ARGS__);

#ifdef QS_DEBUG
    #define LOG_DEBUG(msg, ...) Log::get_instance().core_log_output(Log::LOG_LEVEL_DEBUG, msg, ##__VA_ARGS__);
    #define LOG_TRACE(msg, ...) Log::get_instance().core_log_output(Log::LOG_LEVEL_TRACE, msg, ##__VA_ARGS__);
#else 
    #define LOG_DEBUG(msg, ...)
    #define LOG_TRACE(msg, ...)
#endif