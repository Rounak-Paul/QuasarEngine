#ifdef QS_PLATFORM_WINDOWS

#include "Mutex.h"
#include <Windows.h>

namespace Quasar {

Mutex::Mutex() { }

b8 Mutex::create() {
    internal_data = CreateMutex(0, 0, 0);
    if (!internal_data) {
        LOG_ERROR("Unable to create mutex.");
        return FALSE;
    }
    return TRUE;
}

void Mutex::destroy() {
    if (internal_data) {
        CloseHandle(internal_data);
        internal_data = 0;
    }
}

b8 Mutex::lock() {
    if (internal_data == nullptr) {return FALSE;}
    DWORD result = WaitForSingleObject(internal_data, INFINITE);
    switch (result) {
        // The thread got ownership of the mutex
        case WAIT_OBJECT_0:
            return true;

            // The thread got ownership of an abandoned mutex.
        case WAIT_ABANDONED:
            LOG_ERROR("Mutex lock failed.");
            return false;
    }
    return true;
}

b8 Mutex::unlock() {
    if (!internal_data) {
        return false;
    }
    i32 result = ReleaseMutex(internal_data);
    return result != 0;  // 0 is a failure
}

}

#endif