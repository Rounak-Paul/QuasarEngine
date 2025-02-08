#pragma once

#include <qspch.h>
#include <Platform/Thread.h>
#include <Platform/Mutex.h>
#include <Core/System.h>

namespace Quasar
{
typedef b8 (*pfn_job_start)(void*, void*);
typedef void (*pfn_job_on_complete)(void*);

typedef enum job_type {
    JOB_TYPE_GENERAL = 0x02,
    JOB_TYPE_RESOURCE_LOAD = 0x04,
    JOB_TYPE_GPU_RESOURCE = 0x08,
} job_type;

typedef enum job_priority {
    JOB_PRIORITY_LOW,
    JOB_PRIORITY_NORMAL,
    JOB_PRIORITY_HIGH
} job_priority;

typedef struct job_info {
    job_type type;
    job_priority priority;
    pfn_job_start entry_point;
    pfn_job_on_complete on_success;
    pfn_job_on_complete on_fail;
    void* param_data;
    u32 param_data_size;
    void* result_data;
    u32 result_data_size;
} job_info;

typedef struct job_thread {
    u8 index;
    Thread thread;
    job_info info;
    Mutex info_mutex;
    u32 type_mask;
} job_thread;

typedef struct job_result_entry {
    u16 id;
    pfn_job_on_complete callback;
    u32 param_size;
    void* params;
} job_result_entry;

#define MAX_JOB_RESULTS 512

typedef struct job_system_state {
    b8 running;
    u8 thread_count;
    job_thread job_threads[32];

    RingQueue low_priority_queue;
    RingQueue normal_priority_queue;
    RingQueue high_priority_queue;

    Mutex low_pri_queue_mutex;
    Mutex normal_pri_queue_mutex;
    Mutex high_pri_queue_mutex;

    job_result_entry pending_results[MAX_JOB_RESULTS];
    Mutex result_mutex;
} job_system_state;


typedef struct job_system_config {
    u8 max_job_thread_count;
    u32 type_masks[15];
} job_system_config;

class QS_API JobSystem : public System {
    public:
    JobSystem() {};
    ~JobSystem() = default;
    
    virtual b8 init(void* config) override;
    virtual void shutdown() override;

    void update();
    void submit(job_info info);
    job_info job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size);
    job_info job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type);
    job_info job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type, job_priority priority);

    private:
    job_system_state state;

    void store_result(pfn_job_on_complete callback, u32 param_size, void* params);
    static u32 job_thread_run(void* params);
    void process_queue(RingQueue* queue, Mutex* queue_mutex);
};
} // namespace Quasar
