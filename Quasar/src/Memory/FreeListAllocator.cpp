#include "FreeListAllocator.h"
#include <qspch.h>

namespace Quasar {
FreeListAllocator::FreeListAllocator(size_t pool_size, size_t alignment) : alignment(alignment) {
    memory_pool = std::malloc(pool_size);
    if (!memory_pool) {
        throw std::bad_alloc();
    }
    memset(memory_pool, 0, pool_size);
    memory_pool = reinterpret_cast<char*>(get_aligned(reinterpret_cast<u64>(memory_pool), alignment));
    freeList = static_cast<Block*>(memory_pool);
    freeList->size = pool_size - sizeof(Block) - alignment;
    freeList->next = nullptr;
    freeList->free = true;

    memory_used_track = 0;
    total_allocation_count = 0;

    mutex.create();
}

FreeListAllocator::~FreeListAllocator() {
    std::free(memory_pool);
    memory_pool = nullptr;
    mutex.destroy();
}

void* FreeListAllocator::allocate(size_t size) {
    if (this == nullptr) { return nullptr; }
    // if (size < 128) {
    //     size  = 128;
    // }
    mutex.lock();
    Block* prev = nullptr;
    Block* curr = freeList;

    if (size <= alignment) {
        size = alignment;
    } else {
        size = ((size + alignment - 1) / alignment) * alignment;
    }

    while (curr != nullptr) {
        if (curr->free && curr->size >= size) {
            if (curr->size > size + sizeof(Block)) {
                Block* next = reinterpret_cast<Block*>(reinterpret_cast<char*>(curr) + sizeof(Block) + size);
                next->size = curr->size - size - sizeof(Block);
                next->next = curr->next;
                next->free = true;
                curr->size = size;
                curr->next = next;
            }

            curr->free = false;

            if (prev != nullptr) {
                prev->next = curr->next;
            } else {
                freeList = curr->next;
            }

            void* allocatedMemory = reinterpret_cast<char*>(curr) + sizeof(Block);
            std::memset(allocatedMemory, 0, size);  // Set allocated memory to zero
            memory_used_track += size;
            total_allocation_count += 1;
            mutex.unlock();
            return allocatedMemory;
        }

        prev = curr;
        curr = curr->next;
    }

    std::cerr << "Memory allocation failed: out of memory" << std::endl;
    mutex.unlock();
    return nullptr;
}

void FreeListAllocator::free(void* ptr) {
    if (this == nullptr) { return; }
    if (ptr == nullptr) {
        LOG_WARN("Can not free nullptr.")
        return;
    }
    mutex.lock();
    Block* block = reinterpret_cast<Block*>(reinterpret_cast<char*>(ptr) - sizeof(Block));
    
    if (block->free) {
        std::cerr << "Error: Attempting to free an already free block" << std::endl;
        mutex.unlock();
        throw std::runtime_error("Attempting to free an already free block");
    }

    block->free = true;
    memory_used_track -= block->size;
    total_allocation_count -= 1;

    if (!freeList) {
        freeList = block;
        block->next = nullptr;
    } else {
        Block* curr = freeList;
        Block* prev = nullptr;
        while (curr && curr < block) {
            prev = curr;
            curr = curr->next;
        }

        if (prev) {
            prev->next = block;
        } else {
            freeList = block;
        }

        block->next = curr;

        // Merge adjacent free blocks
        merge_adjacent_free_blocks(prev, block);
    }

    mutex.unlock();
}

void FreeListAllocator::merge_adjacent_free_blocks(Block* prev, Block* block) {
    if (block->next && reinterpret_cast<char*>(block) + sizeof(Block) + block->size == reinterpret_cast<char*>(block->next)) {
        block->size += sizeof(Block) + block->next->size;
        block->next = block->next->next;
    }

    if (prev && reinterpret_cast<char*>(prev) + sizeof(Block) + prev->size == reinterpret_cast<char*>(block)) {
        prev->size += sizeof(Block) + block->size;
        prev->next = block->next;
    }
}

void FreeListAllocator::defragment() {
    mutex.lock();
    Block* curr = freeList;
    while (curr != nullptr) {
        Block* next = curr->next;
        if (next && reinterpret_cast<char*>(curr) + sizeof(Block) + curr->size == reinterpret_cast<char*>(next)) {
            curr->size += sizeof(Block) + next->size;
            curr->next = next->next;
        } else {
            curr = next;
        }
    }
    mutex.unlock();
}

size_t FreeListAllocator::block_size() {
    return sizeof(Block);
}

std::string FreeListAllocator::free_list_string() {
    Block* curr = freeList;
    std::ostringstream out;
    while (curr != nullptr) {
        out << "Block:" << curr << "| size: " << curr->size << " (free: " << std::boolalpha << curr->free << ")\n";
        curr = curr->next;
    }
    return out.str();
}
} // namespace Quasar