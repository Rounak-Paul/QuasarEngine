#pragma once

#include "Defines.h"
#include "FreeListAllocator.h"
#include "LinearAllocator.h"

namespace Quasar
{
class QS_API Memory {
    public:
    static b8 init();
    void shutdown();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    static Memory& get_instance() {return *instance;}
    static FreeListAllocator& get_free_list_allocator() {return *free_list_allocator;}
    static LinearAllocator& get_frame_allocator() {return *frame_allocator;}

    private: 
    Memory() {}
    static Memory* instance;
    static FreeListAllocator* free_list_allocator;
    static LinearAllocator* frame_allocator;
};

#define QS_MEMORY_SYSTEM Memory::get_instance()
#define QSMEM QS_MEMORY_SYSTEM.get_free_list_allocator()
#define QSMEM_FRAME QS_MEMORY_SYSTEM.get_frame_allocator()
} // namespace Quasar
