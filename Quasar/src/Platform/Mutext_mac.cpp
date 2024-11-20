#ifdef QS_PLATFORM_APPLE

#include "Mutex.h"

namespace Quasar {

Mutex::Mutex() {
    
}

b8 Mutex::create() {
    // Initialize
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t mutex;
    i32 result = pthread_mutex_init(&mutex, &mutex_attr);
    if (result != 0) {
        LOG_ERROR("Mutex creation failure!");
        return false;
    }

    // Save off the mutex handle.
    internal_data = std::malloc(sizeof(pthread_mutex_t));
    *(pthread_mutex_t*)internal_data = mutex;
    return true;
}

void Mutex::destroy() {
    i32 result = pthread_mutex_destroy((pthread_mutex_t*)internal_data);
    switch (result) {
        case 0:
            break;
        case EBUSY:
            LOG_ERROR("Unable to destroy mutex: mutex is locked or referenced.");
            break;
        case EINVAL:
            LOG_ERROR("Unable to destroy mutex: the value specified by mutex is invalid.");
            break;
        default:
            LOG_ERROR("An handled error has occurred while destroy a mutex: errno=%i", result);
            break;
    }

    std::free(internal_data);
    internal_data = nullptr;

}

b8 Mutex::lock() {
    // Lock
    if (internal_data == nullptr) {return false;}
    i32 result = pthread_mutex_lock((pthread_mutex_t*)internal_data);
    switch (result) {
        case 0:
            // Success, everything else is a failure.
            return true;
        case EOWNERDEAD:
            LOG_ERROR("Owning thread terminated while mutex still active.");
            return false;
        case EAGAIN:
            LOG_ERROR("Unable to obtain mutex lock: the maximum number of recursive mutex locks has been reached.");
            return false;
        case EBUSY:
            LOG_ERROR("Unable to obtain mutex lock: a mutex lock already exists.");
            return false;
        case EDEADLK:
            LOG_ERROR("Unable to obtain mutex lock: a mutex deadlock was detected.");
            return false;
        default:
            LOG_ERROR("An handled error has occurred while obtaining a mutex lock: errno=%i", result);
            return false;
    }
}

b8 Mutex::unlock() {
    if (internal_data) {
        i32 result = pthread_mutex_unlock((pthread_mutex_t*)internal_data);
        switch (result) {
            case 0:
                return true;
            case EOWNERDEAD:
                LOG_ERROR("Unable to unlock mutex: owning thread terminated while mutex still active.");
                return false;
            case EPERM:
                LOG_ERROR("Unable to unlock mutex: mutex not owned by current thread.");
                return false;
            default:
                LOG_ERROR("An handled error has occurred while unlocking a mutex lock: errno=%i", result);
                return false;
        }
    }

    return false;
}

}

#endif