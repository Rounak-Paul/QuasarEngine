#include "Log.h"

#include <Platform/File.h>
#include <iostream>

namespace Quasar
{
    Log Log::instance;
    b8 Log::is_runtime = false;
    std::chrono::high_resolution_clock::time_point Log::engine_start_time = std::chrono::high_resolution_clock::now();
    std::chrono::high_resolution_clock::time_point Log::current_time;
    File f;

    const  char* level_strings_color[6] = {"\033[1;31m[FATAL]: ", "\033[1;31m[ERROR]: ", "\033[1;33m[WARN] : ", "\033[1;32m[INFO] : ", "\033[1;34m[DEBUG]: ", "\033[1;36m[TRACE]: "};
    const  char* level_strings[6] = {"[FATAL]: ", "[ERROR]: ", "[WARN] : ", "[INFO] : ", "[DEBUG]: ", "[TRACE]: "};

    b8 Log::init() {
        f.open("run.log", File::Mode::WRITE);
        return true;
    }

    void Log::shutdown() {
        f.close();
    }

    void Log::core_log_output(log_level level, const char* msg, ...) {
        char out_message[32000];

        current_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> duration =  current_time - engine_start_time;

        #if QS_PLATFORM_WINDOWS
        va_list arg_ptr;
        #else
        __builtin_va_list arg_ptr;
        #endif
        
        va_start(arg_ptr, msg);
        vsnprintf(out_message, 32000, msg, arg_ptr);
        va_end(arg_ptr);

        char out_consol[32000];
        snprintf(out_consol, sizeof(out_consol), "%011.4f%s%s%s%s\n", duration.count()," \033[1;45m[QUASAR]\033[0m ", level_strings_color[level], out_message, "\033[0m");

        char out_textline[32000];
        snprintf(out_textline, sizeof(out_textline), "%011.4f%s%s%s", duration.count(), " [QUASAR] ", level_strings[level], out_message);

        // TODO: platform-specific output.
        std::cout << out_consol;
        if (f.is_open())
            f.write_line(out_textline);
    }
} // namespace Quasar
