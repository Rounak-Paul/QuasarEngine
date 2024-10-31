#include "Memory.h"

namespace Quasar
{
Memory* Memory::instance = nullptr;
FreeListAllocator* Memory::free_list_allocator = nullptr;
LinearAllocator* Memory::frame_allocator = nullptr;

b8 Memory::init() {
    assert(!instance);
    instance = new Memory();

    free_list_allocator = new FreeListAllocator(GIGABYTES(1), 16);
    frame_allocator = new LinearAllocator(MEGABYTES(256), 16);

    return true;
}

void Memory::shutdown() {
    delete free_list_allocator;
    delete frame_allocator;
    free_list_allocator = nullptr;
    frame_allocator = nullptr;
}
} // namespace Quasar
