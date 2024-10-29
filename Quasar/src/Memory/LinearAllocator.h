#pragma once

#include <iostream>
#include <cstdlib>
#include <cassert>
#include <Platform/Mutex.h>

namespace Quasar
{
class QS_API LinearAllocator {
    public:
    LinearAllocator(size_t size, size_t alignment);
    ~LinearAllocator();

    void* allocate(size_t size);
    void clear();

    private:
    char* m_start;
    size_t m_size;
    size_t m_used;
    size_t m_alignment;
    Mutex mutex;

};
} // namespace Quasar
