#pragma once

#include <qspch.h>

namespace Quasar
{
    class QS_API Mutex {
        public:
        Mutex();
        b8 create();
        void destroy();
        b8 lock();
        b8 unlock();

        private:
        void* internal_data;
    };
} // namespace Quasar
