#pragma once

#include <iostream>
#include <cstdlib>
#include <cassert>
#include <Platform/Mutex.h>

namespace Quasar
{
class QS_API FreeListAllocator {
private:
    struct Block {
        size_t size;
        Block* next;
        bool free;
    };

    Block* freeList;
    void* memory_pool;
    size_t alignment;
    Mutex mutex;
    size_t memory_used_track;
    unsigned int total_allocation_count;

public:
    FreeListAllocator(size_t pool_size, size_t alignment);
    ~FreeListAllocator();

    void* allocate(size_t size);

    void free(void* ptr);

    std::string free_list_string();

    static size_t block_size();
    size_t memory_used() {return memory_used_track;}
    unsigned int allocation_count() {return total_allocation_count;}
    void merge_adjacent_free_blocks(Block* prev, Block* block);
    void defragment();
};

} // namespace Quasar
