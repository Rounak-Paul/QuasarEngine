#pragma once

#include <Memory/Memory.h>
#include <Platform/Mutex.h>

namespace Quasar {

template<typename T>
class FrameDynamicArray {
private:
    T* data = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    Mutex mutex;

    b8 create(size_t initial_capacity = 8) {
        mutex.create();
        size = 0;
        capacity = initial_capacity;
        data = static_cast<T*>(QSMEM_FRAME.allocate(capacity * sizeof(T)));
        assert(data && "Allocation failed!");
        return true;
    }

public:
    FrameDynamicArray() { }
    ~FrameDynamicArray() {destroy();};

    void destroy() {
        if (data) {
            data = nullptr;
        }
    }

    void push_back(const T& value) {
        if (data == nullptr) {
            create();
        }
        mutex.lock();
        if (size == capacity) {
            reserve(capacity * 2);  // Double the capacity if no space left
        }
        new(&data[size]) T(value);  // Placement new
        ++size;
        mutex.unlock();
    }

    void push_back(T&& value) {
        if (data == nullptr) {
            create();
        }
        mutex.lock();
        if (size == capacity) {
            reserve(capacity * 2);  // Double the capacity if no space left
        }
        new(&data[size]) T(std::move(value));  // Placement new with move
        ++size;
        mutex.unlock();
    }

    void pop_back() {
        mutex.lock();
        if (size > 0) {
            data[--size].~T();  // Call destructor for the last element
        }
        mutex.unlock();
    }

    T& operator[](size_t index) {
        assert(index < size && "Index out of bounds!");
        return data[index];
    }

    const T& operator[](size_t index) const {
        assert(index < size && "Index out of bounds!");
        return data[index];
    }

    size_t get_size() const {
        return size;
    }

    size_t get_capacity() const {
        return capacity;
    }

    bool is_empty() const {
        return size == 0;
    }

    void reserve(size_t new_capacity) {
        if (data == nullptr) {
            create(new_capacity);
        }
        if (new_capacity <= capacity) {
            return;  // No need to reserve if the current capacity is sufficient
        }
        mutex.lock();
        T* new_data = static_cast<T*>(QSMEM_FRAME.allocate(new_capacity * sizeof(T)));
        assert(new_data && "Allocation failed!");

        for (size_t i = 0; i < size; ++i) {
            // Use placement new to move construct elements in the new memory
            new(new_data + i) T(std::move(data[i]));  
            // Explicitly call destructor for old elements
            data[i].~T();  
        }

        data = new_data;
        capacity = new_capacity;
        mutex.unlock();
    }

    void resize(size_t new_size) {
        if (data == nullptr) {
            create();
        }
        mutex.lock();
        if (new_size > capacity) {
            reserve(new_size);  // Reserve more capacity if needed
        }

        for (size_t i = size; i < new_size; ++i) {
            // Default-construct new elements
            new(&data[i]) T();  
        }

        for (size_t i = new_size; i < size; ++i) {
            // Call destructor for elements that are no longer needed
            data[i].~T();
        }

        size = new_size;
        mutex.unlock();
    }

    void clear() {
        mutex.lock();
        size = 0;
        capacity = 0;
        mutex.unlock();
    }

    T* get_data() {
        return data;
    }
};

} // namespace Quasar