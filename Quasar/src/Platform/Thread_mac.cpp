#ifdef QS_PLATFORM_APPLE

#include "Thread.h"

#include "Platform.h"

#include <pthread.h>
#include <errno.h>        // For error reporting

namespace Quasar
{
    b8 Thread::start(pfn_thread_start start_function_ptr, void *params, b8 auto_detach) {
        if (!start_function_ptr) {
            return false;
        }

        // pthread_create uses a function pointer that returns void*, so cold-cast to this type.
        i32 result = pthread_create((pthread_t*)&thread_id, 0, (void* (*)(void*))start_function_ptr, params);
        if (result != 0) {
            switch (result) {
                case EAGAIN:
                    LOG_ERROR("Failed to create thread: insufficient resources to create another thread.");
                    return false;
                case EINVAL:
                    LOG_ERROR("Failed to create thread: invalid settings were passed in attributes..");
                    return false;
                default:
                    LOG_ERROR("Failed to create thread: an unhandled error has occurred. errno=%i", result);
                    return false;
            }
        }
        LOG_DEBUG("Starting process on thread id: %#x", thread_id);

        // Only save off the handle if not auto-detaching.
        if (!auto_detach) {
            internal_data = QSMEM.allocate(sizeof(u64));
            *(u64*)internal_data = thread_id;
        } else {
            // If immediately detaching, make sure the operation is a success.
            result = pthread_detach((pthread_t)thread_id);
            if (result != 0) {
                switch (result) {
                    case EINVAL:
                        LOG_ERROR("Failed to detach newly-created thread: thread is not a joinable thread.");
                        return false;
                    case ESRCH:
                        LOG_ERROR("Failed to detach newly-created thread: no thread with the id %#x could be found.", thread_id);
                        return false;
                    default:
                        LOG_ERROR("Failed to detach newly-created thread: an unknown error has occurred. errno=%i", result);
                        return false;
                }
            }
        }

        return true;
    }

    void Thread::destroy() {
        cancel();
    }

    void Thread::detach() {
        if (internal_data) {
            i32 result = pthread_detach(*(pthread_t*)internal_data);
            if (result != 0) {
                switch (result) {
                    case EINVAL:
                        LOG_ERROR("Failed to detach thread: thread is not a joinable thread.");
                        break;
                    case ESRCH:
                        LOG_ERROR("Failed to detach thread: no thread with the id %#x could be found.", thread_id);
                        break;
                    default:
                        LOG_ERROR("Failed to detach thread: an unknown error has occurred. errno=%i", result);
                        break;
                }
            }
            QSMEM.free(internal_data);
            internal_data = nullptr;
        }
    }

    void Thread::cancel() {
        if (internal_data) {
            i32 result = pthread_cancel(*(pthread_t*)internal_data);
            if (result != 0) {
                switch (result) {
                    case ESRCH:
                        LOG_ERROR("Failed to cancel thread: no thread with the id %#x could be found.", thread_id);
                        break;
                    default:
                        LOG_ERROR("Failed to cancel thread: an unknown error has occurred. errno=%i", result);
                        break;
                }
            }
            QSMEM.free(internal_data);
            internal_data = nullptr;
            thread_id = 0;
        }
    }

    b8 Thread::is_active() {
        // TODO: Find a better way to verify this.
        return internal_data != nullptr;
    }

    void Thread::sleep(u64 ms) {
        platform_sleep(ms);
    }

    u64 Thread::get_thread_id() {
        return (u64)pthread_self();
    }
} // namespace Quasar


#endif