#pragma once

#include <qspch.h>

namespace Quasar
{
    // A function pointer to be invoked when the thread starts.
    typedef u32 (*pfn_thread_start)(void *);

    class QS_API Thread {
        public:
        b8 start(pfn_thread_start start_function_ptr, void *params, b8 auto_detach);
        void destroy();
        void detach();
        void cancel();
        b8 is_active();

        // To be called within the created thread
        void sleep(u64 ms);
        
        // To be called within the created thread
        static u64 get_thread_id();

        private:
        void* internal_data;
        u64 thread_id;
        b8 is_created = FALSE;
    };
} // namespace Quasar
