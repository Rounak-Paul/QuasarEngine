#include "JobSystem.h"
#include <Core/SystemManager.h>

namespace Quasar
{
void JobSystem::store_result(pfn_job_on_complete callback, u32 param_size, void* params) {
    // Create the new entry.
    job_result_entry entry;
    entry.id = INVALID_ID_U16;
    entry.param_size = param_size;
    entry.callback = callback;
    if (entry.param_size > 0) {
        // Take a copy, as the job is destroyed after this.
        entry.params = QSMEM.allocate(param_size);
        memcpy(entry.params, params, param_size);
    } else {
        entry.params = 0;
    }

    // Lock, find a free space, store, unlock.
    if (!state.result_mutex.lock()) {
        LOG_ERROR("Failed to obtain mutex lock for storing a result! Result storage may be corrupted.");
    }
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        if (state.pending_results[i].id == INVALID_ID_U16) {
            state.pending_results[i] = entry;
            state.pending_results[i].id = i;
            break;
        }
    }
    if (!state.result_mutex.unlock()) {
        LOG_ERROR("Failed to release mutex lock for result storage, storage may be corrupted.");
    }
}

u32 JobSystem::job_thread_run(void* params) {
    u32 index = *(u32*)params;
    job_thread* thread = &QS_JOB_SYSTEM.state.job_threads[index];
    u64 thread_id = thread->thread.get_thread_id();
    LOG_DEBUG("Starting job thread #%i (id=%#x, type=%#x).", thread->index, thread_id, thread->type_mask);

    // A mutex to lock info for this thread.
    if (!thread->info_mutex.create()) {
        LOG_ERROR("Failed to create job thread mutex! Aborting thread.");
        return 0;
    }

    // Run forever, waiting for jobs.
    while (true) {
        if (!QS_JOB_SYSTEM.state.running || !thread) {
            break;
        }

        // Lock and grab a copy of the info
        if (!thread->info_mutex.lock()) {
            LOG_ERROR("Failed to obtain lock on job thread mutex!");
        }
        job_info info = thread->info;
        if (!thread->info_mutex.unlock()) {
            LOG_ERROR("Failed to release lock on job thread mutex!");
        }

        if (info.entry_point) {
            b8 result = info.entry_point(info.param_data, info.result_data);

            // Store the result to be executed on the main thread later.
            // Note that store_result takes a copy of the result_data
            // so it does not have to be held onto by this thread any longer.
            if (result && info.on_success) {
                QS_JOB_SYSTEM.store_result(info.on_success, info.result_data_size, info.result_data);
            } else if (!result && info.on_fail) {
                QS_JOB_SYSTEM.store_result(info.on_fail, info.result_data_size, info.result_data);
            }

            // Clear the param data and result data.
            if (info.param_data) {
                QSMEM.free(info.param_data);
            }
            if (info.result_data) {
                QSMEM.free(info.result_data);
            }

            // Lock and reset the thread's info object
            if (!thread->info_mutex.lock()) {
                LOG_ERROR("Failed to obtain lock on job thread mutex!");
            }
            memset(&thread->info, 0, sizeof(job_info));
            if (!thread->info_mutex.unlock()) {
                LOG_ERROR("Failed to release lock on job thread mutex!");
            }
        }

        if (QS_JOB_SYSTEM.state.running) {
            // TODO: Should probably find a better way to do this, such as sleeping until
            // a request comes through for a new job.
            thread->thread.sleep(10);
        } else {
            break;
        }
    }

    // Destroy the mutex for this thread.
    thread->info_mutex.destroy();
    return 1;
}

b8 JobSystem::init(void* config) {
    job_system_config* cfg = (job_system_config*)config;
    state.running = true;

    state.low_priority_queue.create(sizeof(job_info), 1024, 0);
    state.normal_priority_queue.create(sizeof(job_info), 1024, 0);
    state.high_priority_queue.create(sizeof(job_info), 1024, 0);
    state.thread_count = cfg->max_job_thread_count;

    // Invalidate all result slots
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        state.pending_results[i].id = INVALID_ID_U16;
    }

    LOG_DEBUG("Main thread id is: %#x", Thread::get_thread_id());

    LOG_DEBUG("Spawning %i job threads.", state.thread_count);

    for (u8 i = 0; i < state.thread_count; ++i) {
        state.job_threads[i].index = i;
        state.job_threads[i].type_mask = cfg->type_masks[i];
        if (!state.job_threads[i].thread.start(job_thread_run, &state.job_threads[i].index, false)) {
            LOG_FATAL("OS Error in creating job thread. Application cannot continue.");
            return false;
        }
        memset(&state.job_threads[i].info, 0, sizeof(job_info));
    }

    // Create needed mutexes
    if (!state.result_mutex.create()) {
        LOG_ERROR("Failed to create result mutex!.");
        return false;
    }
    if (!state.low_pri_queue_mutex.create()) {
        LOG_ERROR("Failed to create low priority queue mutex!.");
        return false;
    }
    if (!state.normal_pri_queue_mutex.create()) {
        LOG_ERROR("Failed to create normal priority queue mutex!.");
        return false;
    }
    if (!state.high_pri_queue_mutex.create()) {
        LOG_ERROR("Failed to create high priority queue mutex!.");
        return false;
    }

    return true;
}

void JobSystem::shutdown() {
    state.running = false;

    u64 thread_count = state.thread_count;

    // Check for a free thread first.
    for (u8 i = 0; i < thread_count; ++i) {
        state.job_threads[i].thread.destroy();
    }
    state.low_priority_queue.destroy();
    state.normal_priority_queue.destroy();
    state.high_priority_queue.destroy();

    // Destroy mutexes
    state.result_mutex.destroy();
    state.low_pri_queue_mutex.destroy();
    state.normal_pri_queue_mutex.destroy();
    state.high_pri_queue_mutex.destroy();
}

void JobSystem::process_queue(RingQueue* queue, Mutex* queue_mutex) {
    u64 thread_count = state.thread_count;

    // Check for a free thread first.
    while (queue->length > 0) {
        job_info info;
        if (!queue->peek(&info)) {
            break;
        }

        b8 thread_found = false;
        for (u8 i = 0; i < thread_count; ++i) {
            job_thread* thread = &state.job_threads[i];
            if ((thread->type_mask & info.type) == 0) {
                continue;
            }

            // Check that the job thread can handle the job type.
            if (!thread->info_mutex.lock()) {
                LOG_ERROR("Failed to obtain lock on job thread mutex!");
            }
            if (!thread->info.entry_point) {
                // Make sure to remove the entry from the queue.
                if (!queue_mutex->lock()) {
                    LOG_ERROR("Failed to obtain lock on queue mutex!");
                }
                queue->dequeue(&info);
                if (!queue_mutex->unlock()) {
                    LOG_ERROR("Failed to release lock on queue mutex!");
                }
                thread->info = info;
                LOG_DEBUG("Assigning job to thread: %u", thread->index);
                thread_found = true;
            }
            if (!thread->info_mutex.unlock()) {
                LOG_ERROR("Failed to release lock on job thread mutex!");
            }

            // Break after unlocking if an available thread was found.
            if (thread_found) {
                break;
            }
        }

        // This means all of the threads are currently handling a job,
        // So wait until the next update and try again.
        if (!thread_found) {
            break;
        }
    }
}

void JobSystem::update() {
    if (!state.running) {
        return;
    }

    process_queue(&state.high_priority_queue, &state.high_pri_queue_mutex);
    process_queue(&state.normal_priority_queue, &state.normal_pri_queue_mutex);
    process_queue(&state.low_priority_queue, &state.low_pri_queue_mutex);

    // Process pending results.
    for (u16 i = 0; i < MAX_JOB_RESULTS; ++i) {
        // Lock and take a copy, unlock.
        if (!state.result_mutex.lock()) {
            LOG_ERROR("Failed to obtain lock on result mutex!");
        }
        job_result_entry entry = state.pending_results[i];
        if (!state.result_mutex.unlock()) {
            LOG_ERROR("Failed to release lock on result mutex!");
        }

        if (entry.id != INVALID_ID_U16) {
            // Execute the callback.
            entry.callback(entry.params);

            if (entry.params) {
                QSMEM.free(entry.params);
            }

            // Lock actual entry, invalidate and clear it
            if (!state.result_mutex.lock()) {
                LOG_ERROR("Failed to obtain lock on result mutex!");
            }
            memset(&state.pending_results[i], 0, sizeof(job_result_entry));
            state.pending_results[i].id = INVALID_ID_U16;
            if (!state.result_mutex.unlock()) {
                LOG_ERROR("Failed to release lock on result mutex!");
            }
        }
    }
}

void JobSystem::submit(job_info info) {
    u64 thread_count = state.thread_count;
    RingQueue* queue = &state.normal_priority_queue;
    Mutex* queue_mutex = &state.normal_pri_queue_mutex;

    // If the job is high priority, try to kick it off immediately.
    if (info.priority == JOB_PRIORITY_HIGH) {
        queue = &state.high_priority_queue;
        queue_mutex = &state.high_pri_queue_mutex;

        // Check for a free thread that supports the job type first.
        for (u8 i = 0; i < thread_count; ++i) {
            job_thread* thread = &state.job_threads[i];
            if (state.job_threads[i].type_mask & info.type) {
                b8 found = false;
                if (!thread->info_mutex.lock()) {
                    LOG_ERROR("Failed to obtain lock on job thread mutex!");
                }
                if (!state.job_threads[i].info.entry_point) {
                    LOG_DEBUG("Job immediately submitted on thread %i", state.job_threads[i].index);
                    state.job_threads[i].info = info;
                    found = true;
                }
                if (!thread->info_mutex.unlock()) {
                    LOG_ERROR("Failed to release lock on job thread mutex!");
                }
                if (found) {
                    return;
                }
            }
        }
    }

    // If this point is reached, all threads are busy (if high) or it can wait a frame.
    // Add to the queue and try again next cycle.
    if (info.priority == JOB_PRIORITY_LOW) {
        queue = &state.low_priority_queue;
        queue_mutex = &state.low_pri_queue_mutex;
    }

    // NOTE: Locking here in case the job is submitted from another job/thread.
    if (!queue_mutex->lock()) {
        LOG_ERROR("Failed to obtain lock on queue mutex!");
    }
    queue->enqueue(&info);
    if (!queue_mutex->unlock()) {
        LOG_ERROR("Failed to release lock on queue mutex!");
    }
    LOG_DEBUG("Job queued.");
}

job_info JobSystem::job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size) {
    return job_create(entry_point, on_success, on_fail, param_data, param_data_size, result_data_size, JOB_TYPE_GENERAL, JOB_PRIORITY_NORMAL);
}

job_info JobSystem::job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type) {
    return job_create(entry_point, on_success, on_fail, param_data, param_data_size, result_data_size, type, JOB_PRIORITY_NORMAL);
}

job_info JobSystem::job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type, job_priority priority) {
    job_info job;
    job.entry_point = entry_point;
    job.on_success = on_success;
    job.on_fail = on_fail;
    job.type = type;
    job.priority = priority;

    job.param_data_size = param_data_size;
    if (param_data_size) {
        job.param_data = QSMEM.allocate(param_data_size);
        memcpy(job.param_data, param_data, param_data_size);
    } else {
        job.param_data = 0;
    }

    job.result_data_size = result_data_size;
    if (result_data_size) {
        job.result_data = QSMEM.allocate(result_data_size); 
    } else {
        job.result_data = 0;
    }
    return job;
}
} // namespace Quasar
