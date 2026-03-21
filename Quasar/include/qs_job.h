#ifndef QS_JOB_H
#define QS_JOB_H

#include <stdint.h>

/// Opaque job system handle.
typedef struct Qs_JobSystem Qs_JobSystem;

/// Opaque counter used to track completion of a batch of jobs.
typedef struct Qs_JobCounter Qs_JobCounter;

/// Job function signature.
typedef void (*Qs_JobFn)(void* data);

/// Descriptor for submitting a job.
typedef struct Qs_JobDesc {
    Qs_JobFn  fn;           ///< Function to execute.
    void*     data;         ///< User data passed to fn.
} Qs_JobDesc;

typedef struct Qs_SystemDesc Qs_SystemDesc;

/// Returns the system descriptor for the job system.
/// Register with qs_system_register() — the engine does this automatically.
Qs_SystemDesc qs_job_system_desc(void);

/// Allocates a counter for tracking job completion.
Qs_JobCounter* qs_job_counter_create(Qs_JobSystem* system);

/// Frees a counter. Must only be called after all jobs using it have completed.
void qs_job_counter_destroy(Qs_JobSystem* system, Qs_JobCounter* counter);

/// Submits a single job. The counter is incremented before dispatch and
/// decremented when the job finishes. Pass NULL for counter if tracking
/// is not needed.
void qs_job_dispatch(Qs_JobSystem* system, const Qs_JobDesc* job,
                     Qs_JobCounter* counter);

/// Submits a batch of jobs sharing the same counter.
void qs_job_dispatch_batch(Qs_JobSystem* system, const Qs_JobDesc* jobs,
                           uint32_t count, Qs_JobCounter* counter);

/// Blocks the calling thread until the counter reaches zero.
/// The calling thread assists by executing pending jobs while waiting.
void qs_job_wait(Qs_JobSystem* system, Qs_JobCounter* counter);

/// Returns the number of worker threads.
uint32_t qs_job_system_thread_count(const Qs_JobSystem* system);

#endif
