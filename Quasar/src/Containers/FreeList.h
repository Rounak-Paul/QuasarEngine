#pragma once
#include <qspch.h>
#include <Platform/Mutex.h>
#include <list>
#include <sstream>
#include <algorithm> // For std::max

namespace Quasar {

class FreeList {
    private:
    struct memory_region {
        u64 start;
        u64 size;
    };

    u64 buffer_size;
    u64 alignment;
    std::list<memory_region> free_list;

    // Helper function to align a given address
    u64 align_address(u64 address) const {
        return (address + alignment - 1) & ~(alignment - 1);
    }

    public:
    // Constructor to initialize the buffer with a certain size and alignment
    FreeList(u64 initial_size, u32 alignment) : buffer_size(initial_size), alignment(alignment) {
        free_list.push_back({0, initial_size});
    }

    // Method to allocate memory for a new geometry
    u64 allocate_memory(u64 size) {
        size = align_address(size); // Ensure the size is aligned
        // Iterate through the free list to find a suitable memory region
        for (auto it = free_list.begin(); it != free_list.end(); ++it) {
            u64 aligned_start = align_address(it->start);
            u64 padding = aligned_start - it->start;
            u64 total_size_needed = size + padding;

            if (it->size >= total_size_needed) {
                // Found a suitable memory region
                u64 start = aligned_start;
                it->start = aligned_start + size;
                it->size -= total_size_needed;
                if (it->size == 0) {
                    // If the region is fully used, remove it from the free list
                    free_list.erase(it);
                }
                return start;
            }
        }
        // No suitable region found
        return -1;
    }

    // Method to free up memory region previously used by geometry
    void free_memory(u64 start, u64 size) {
        size = align_address(size); // Ensure the size is aligned
        // Find the position to insert the memory region back into the free list
        auto it = free_list.begin();
        while (it != free_list.end() && it->start < start)
            ++it;

        // Insert the memory region back into the free list
        auto inserted_region = free_list.insert(it, {start, size});

        // Merge with the previous region if contiguous
        if (inserted_region != free_list.begin()) {
            auto prev = std::prev(inserted_region);
            if (prev->start + prev->size == start) {
                prev->size += size;
                free_list.erase(inserted_region);
                inserted_region = prev;
            }
        }

        // Merge with the next region if contiguous
        auto next = std::next(inserted_region);
        if (next != free_list.end() && inserted_region->start + inserted_region->size == next->start) {
            inserted_region->size += next->size;
            free_list.erase(next);
        }
    }

    // Method to resize the buffer
    void resize(size_t new_size) {
        if (new_size > buffer_size) {
            auto it = std::prev(free_list.end());
            if ((it->start + it->size) == buffer_size) {
                it->size += (new_size - buffer_size);
            }
            else {
                free_list.insert(it, {buffer_size, (new_size - buffer_size)});
            }
        } else if (new_size < buffer_size) {
            // If new_size is smaller than current size, update the free list accordingly
            QS_CORE_ERROR("NOT IMPLEMENTED - Class FreeList::resize()")
        }
        buffer_size = new_size;
    }

    // Method to get a string representation of the free list
    std::string free_list_string() const {
        std::ostringstream out;
        for (const auto& region : free_list) {
            out << "Block start: " << region.start << " | size: " << region.size << "\n";
        }
        return out.str();
    }
};

}