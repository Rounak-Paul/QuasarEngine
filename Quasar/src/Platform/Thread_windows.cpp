#ifdef QS_PLATFORM_WINDOWS

#include "Thread.h"

#include "Platform.h"

#include <Windows.h>

namespace Quasar
{
b8 Thread::start(pfn_thread_start start_function_ptr, void *params, b8 auto_detach) {
    if (!start_function_ptr) {
        return false;
    }

    internal_data = CreateThread(
        0,
        0,                                           // Default stack size
        (LPTHREAD_START_ROUTINE)start_function_ptr,  // function ptr
        params,                                      // param to pass to thread
        0,
        (DWORD *)&thread_id);
    LOG_DEBUG("Starting process on thread id: %#x", thread_id);
    if (!internal_data) {
        return false;
    }
    if (auto_detach) {
        CloseHandle(internal_data);
    }
    return true;
}

void Thread::destroy() {
    if (internal_data) {
        DWORD exit_code;
        GetExitCodeThread(internal_data, &exit_code);
        CloseHandle((HANDLE)internal_data);
        internal_data = nullptr;
        thread_id = 0;
    }
}

void Thread::detach() {
    if (internal_data) {
        CloseHandle(internal_data);
        internal_data = nullptr;
    }
}

void Thread::cancel() {
    if (internal_data) {
        TerminateThread(internal_data, 0);
        internal_data = nullptr;
    }
}

b8 Thread::is_active() {
    if (internal_data) {
        DWORD exit_code = WaitForSingleObject(internal_data, 0);
        if (exit_code == WAIT_TIMEOUT) {
            return true;
        }
    }
    return false;
}

void Thread::sleep(u64 ms) {
    platform_sleep(ms);
}

u64 Thread::get_thread_id() {
    return (u64)GetCurrentThreadId();
}
} // namespace Quasar


#endif