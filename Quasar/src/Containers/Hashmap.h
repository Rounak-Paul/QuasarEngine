#pragma once

#include <Defines.h>
#include <Core/Log.h>
#include "DynamicArray.h"

namespace Quasar {
template <typename T>
class Hashmap {
private:
    typedef struct hash_unit {
        T data;
        b8 has_data;
    } hash_unit;

    DynamicArray<hash_unit> memory;

    u64 hash_name(const String& name) const {
        const char* data = name.c_str();
        static const u64 multiplier = 97;
        u64 hash = 0;

        for (const u8* us = (const u8*)data; *us; us++) {
            hash = hash * multiplier + *us;
        }

        hash %= memory.get_size();
        return hash;
    }

public:
    Hashmap() {}
    b8 create(u32 element_count = 17) {
        // memory.create();
        return resize(element_count);
    }
    void destroy() {
        memory.destroy();
    }

    ~Hashmap() {
        // Clean up memory if needed
        destroy();
    }

    b8 resize(u32 element_count) {
        memory.resize(element_count);
        return true;
    }

    b8 set(const String& name, T value) {
        if ((name.length() == 0)) return false;
        if (memory.get_size() == 0) {
            LOG_ERROR("Hashmap memory can not be empty")
            return false;
        }

        u64 hash = hash_name(name);
        if (memory[hash].has_data == true) {
            // LOG_WARN("Hashmap data overwrite")
        }
        memory[hash] = {value, true};
        return true;
    }

    T* get(const String& name) {
        if (name.length() == 0) return nullptr;

        u64 hash = hash_name(name);
        if (!memory[hash].has_data) return nullptr;
        return &memory[hash].data;
    }

    void erase(const String& name) {
        if (name.length() == 0) return;
        u64 hash = hash_name(name);
        if (memory[hash].has_data) {
            memory[hash].has_data = false;
        }
    }

    void clear() {
        if (!memory.get_data()) return;
        u32 s = memory.get_size();
        memory.clear();
        resize(s);
    }

// TODO: add method for fill
};
}