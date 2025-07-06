#include "Platform.h"
#ifdef QS_PLATFORM_WINDOWS
#include <windows.h>
#include <psapi.h>
#elif QS_PLATFORM_APPLE
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/mach_init.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace Quasar
{
    size_t get_current_memory_usage() {
        size_t result = 0;
    #if defined(QS_PLATFORM_WINDOWS)
        // Windows specific code to get memory usage
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            result = pmc.WorkingSetSize;
        }
    #elif defined(QS_PLATFORM_LINUX)
        // Linux specific code to get memory usage
        // Read /proc/self/statm file to get memory usage
        FILE* file = fopen("/proc/self/statm", "r");
        if (file) {
            unsigned long size, resident, share, text, lib, data, dt;
            if (fscanf(file, "%lu %lu %lu %lu %lu %lu %lu", &size, &resident, &share, &text, &lib, &data, &dt) == 7) {
                result = resident * sysconf(_SC_PAGESIZE);
            }
            fclose(file);
        }
    #elif defined(QS_PLATFORM_APPLE)
        // macOS specific code to get memory usage
        struct mach_task_basic_info info;
        mach_msg_type_number_t size = sizeof(info);
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &size) == KERN_SUCCESS) {
            result = info.resident_size;
        }
    #endif
        return result;
    }

    i32 platform_get_processor_count() {
    #ifdef QS_PLATFORM_APPLE
        int mib[2];
        int num_cpus;
        size_t len = sizeof(num_cpus);

        mib[0] = CTL_HW;
        mib[1] = HW_NCPU;

        if (sysctl(mib, 2, &num_cpus, &len, NULL, 0) == -1) {
            perror("sysctl");
            return -1;
        }

        return num_cpus;

    #elif QS_PLATFORM_WINDOWS
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return sysinfo.dwNumberOfProcessors;

    #endif
    }

    void platform_sleep(u64 ms) {
    #ifdef QS_PLATFORM_WINDOWS
    Sleep(ms);
    #else
    usleep(ms * 1000);  // usleep takes microseconds
    #endif
    }
} // namespace Quasar
