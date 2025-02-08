#pragma once

#include <Memory/Memory.h>
#include <Platform/Mutex.h>

namespace Quasar {

template<typename T>
class DynamicArray {
private:
    T* data = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    Mutex mutex;

    b8 create(size_t initial_capacity = 8) {
        mutex.create();
        size = 0;
        capacity = initial_capacity;
        data = static_cast<T*>(QSMEM.allocate(capacity * sizeof(T)));
        assert(data && "Allocation failed!");
        return true;
    }

public:
    DynamicArray() { }
    ~DynamicArray() {destroy();};
    
    // Copy Constructor
    DynamicArray(const DynamicArray& other) {
        mutex.create();
        if (other.data) {
            size = other.size;
            capacity = other.capacity;
            data = static_cast<T*>(QSMEM.allocate(capacity * sizeof(T)));
            assert(data && "Allocation failed!");

            // Deep copy the elements
            for (size_t i = 0; i < size; ++i) {
                new(&data[i]) T(other.data[i]);
            }
        }
    }

    // Copy Assignment Operator
    DynamicArray& operator=(const DynamicArray& other) {
        if (this != &other) {
            destroy();  // Clean up existing resources

            mutex.create();
            if (other.data) {
                size = other.size;
                capacity = other.capacity;
                data = static_cast<T*>(QSMEM.allocate(capacity * sizeof(T)));
                assert(data && "Allocation failed!");

                // Deep copy the elements
                for (size_t i = 0; i < size; ++i) {
                    new(&data[i]) T(other.data[i]);
                }
            }
        }
        return *this;
    }

    // Move Constructor
    DynamicArray(DynamicArray&& other) noexcept {
        destroy();

        // Move the mutex resource from the other object
        mutex = std::move(other.mutex);
        size = other.size;
        capacity = other.capacity;
        data = other.data;

        // Nullify the source to prevent double deletion
        other.size = 0;
        other.capacity = 0;
        other.data = nullptr;
    }

    DynamicArray& operator=(DynamicArray&& other) noexcept {
        if (this != &other) {
            destroy();  // Clean up existing resources

            size = other.size;
            capacity = other.capacity;
            data = other.data;

            // Move the mutex resource from the other object
            mutex = std::move(other.mutex);

            // Nullify the source to prevent double deletion
            other.size = 0;
            other.capacity = 0;
            other.data = nullptr;
        }
        return *this;
    }

    DynamicArray(std::initializer_list<T> init_list) {
        create(init_list.size());
        size = init_list.size();
        size_t index = 0;
        for (const T& value : init_list) {
            new (&data[index++]) T(value); // Use placement new to initialize elements
        }
    }

    void destroy() {
        if (data) {
            QSMEM.free(data);  // Free the allocated memory
            data = nullptr;
            size = 0;
            capacity = 0;
        }
    }

    T* begin() { return data; }
    T* end() { return data + size; }
    const T* begin() const { return data; }
    const T* end() const { return data + size; }

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

    void pop_at(size_t index) {
        mutex.lock();
        assert(index < size && "Index out of bounds!");

        data[index].~T();  // Destroy element

        if (index < size - 1) {
            std::memmove(&data[index], &data[index + 1], (size - index - 1) * sizeof(T)); // Fast memory move
        }

        --size;
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
        T* new_data = static_cast<T*>(QSMEM.allocate(new_capacity * sizeof(T)));
        assert(new_data && "Allocation failed!");

        for (size_t i = 0; i < size; ++i) {
            // Use placement new to move construct elements in the new memory
            new(new_data + i) T(std::move(data[i]));  
            // Explicitly call destructor for old elements
            data[i].~T();  
        }

        QSMEM.free(data);  // Free old memory
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
        if (data) {
            mutex.lock();
            size = 0;
            mutex.unlock();
        }
    }

    // T* get_data() {
    //     return data;
    // }

    T* get_data() const {
        return data;
    }
};

} // namespace Quasar
