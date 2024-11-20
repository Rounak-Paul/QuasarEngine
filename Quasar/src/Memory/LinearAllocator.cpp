#include "LinearAllocator.h"

namespace Quasar
{
    LinearAllocator::LinearAllocator(size_t size, size_t alignment)
        : m_size(size), m_used(0), m_alignment(alignment) {
        m_start = (char*)malloc(size + alignment - 1);
        assert(m_start != nullptr && "Failed to allocate memory");
        // Align the start pointer
        m_start = (char*)(((uintptr_t)m_start + (alignment - 1)) & ~(alignment - 1));
        mutex.create();
    }

    LinearAllocator::~LinearAllocator() {
        free(m_start);
        m_start = nullptr;
        mutex.destroy();
    }

    void* LinearAllocator::allocate(size_t size) {
        mutex.lock();
        uintptr_t currentAddress = (uintptr_t)(m_start + m_used);
        size_t padding = (m_alignment - (currentAddress & (m_alignment - 1))) & (m_alignment - 1);
        if (m_used + size + padding > m_size) {
            assert(false && "Allocator overflow");
            return nullptr;
        }

        uintptr_t alignedAddress = currentAddress + padding;
        m_used += size + padding;
        mutex.unlock();
        return (void*)alignedAddress;
    }

    void LinearAllocator::clear() {
        mutex.lock();
        m_used = 0;
        mutex.unlock();
    }
} // namespace Quasar
