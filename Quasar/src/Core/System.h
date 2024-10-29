#pragma once

#include <qspch.h>

namespace Quasar
{
    class QS_API System 
    {
        public:
        System() {};
        virtual ~System() = default;

        virtual b8 init(void* config) {return true;}
        virtual void shutdown() {}

        private:
    };
} // namespace Quasar
