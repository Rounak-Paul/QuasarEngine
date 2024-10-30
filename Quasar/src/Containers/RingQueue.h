#pragma once

#include <Defines.h>
#include <Core/Log.h>
#include <Memory/Memory.h>

namespace Quasar
{
    class RingQueue {
        public:
        u32 length;
        u32 stride;
        u32 capacity;
        void* block;
        b8 owns_memory;
        i32 head;
        i32 tail;
        
        b8 create(u32 stride, u32 capacity, void* memory) {
            length = 0;
            this->capacity = capacity;
            this->stride = stride;
            head = 0;
            tail = -1;
            if (memory) {
                owns_memory = false;
                block = memory;
            } else {
                owns_memory = true;
                block = QSMEM.allocate(capacity * stride);
                memset(block, 0, capacity * stride);
            }
            return true;
        }

        void destroy() {
            if (owns_memory) {
                QSMEM.free(block);
            }
            length = 0;
            stride = 0;
            capacity = 0;
            block = nullptr;
            owns_memory = false;
            head = 0;
            tail = 0;
        }

        b8 enqueue(void* value) {
            if (value) {
                if (length == capacity) {
                    LOG_ERROR("ring_queue_enqueue - Attempted to enqueue value in full ring queue: %p", this);
                    return false;
                }
                tail = (tail + 1) % capacity;
                memcpy(static_cast<uint8_t*>(block) + (tail * stride), value, stride);
                length++;
                return true;
            }
            LOG_ERROR("ring_queue_enqueue requires valid pointers to queue and value.");
            return false;
        }

        b8 dequeue(void* out_value) {
            if (out_value) {
                if (length == 0) {
                    LOG_ERROR("ring_queue_dequeue - Attempted to dequeue value in empty ring queue: %p", this);
                    return false;
                }

                memcpy(out_value, static_cast<uint8_t*>(block) + (head * stride), stride);
                head = (head + 1) % capacity;
                length--;
                return true;
            }

            LOG_ERROR("ring_queue_dequeue requires valid pointers to queue and out_value.");
            return false;
        }

        b8 peek(void* out_value) {
            if (out_value) {
                if (length == 0) {
                    LOG_ERROR("ring_queue_peek - Attempted to peek value in empty ring queue: %p", this);
                    return false;
                }

                memcpy(out_value, static_cast<uint8_t*>(block) + (head * stride), stride);
                return true;
            }
            LOG_ERROR("ring_queue_peek requires valid pointers to queue and out_value.");
            return false;
        }
    };
} // namespace Quasar