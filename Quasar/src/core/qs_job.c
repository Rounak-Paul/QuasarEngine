#include "qs_job.h"
#include <causality.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif

/* ── Ring buffer job queue ──────────────────────────────────── */

#define QS_JOB_QUEUE_CAP 4096

typedef struct {
    Qs_JobFn        fn;
    void*           data;
    Qs_JobCounter*  counter;
} Qs_JobEntry;

typedef struct {
    Qs_JobEntry entries[QS_JOB_QUEUE_CAP];
    uint32_t    head;
    uint32_t    tail;
    Ca_Mutex*   mutex;
    Ca_CondVar* cond;
} Qs_JobQueue;

/* ── Counter ────────────────────────────────────────────────── */

struct Qs_JobCounter {
    volatile long value;
    Ca_Mutex*     mutex;
    Ca_CondVar*   cond;
};

/* ── Job system ─────────────────────────────────────────────── */

struct Qs_JobSystem {
    Qs_JobQueue queue;
    Ca_Thread** threads;
    uint32_t    num_threads;
    volatile int running;
};

/* ── Queue operations ───────────────────────────────────────── */

static bool queue_push(Qs_JobQueue* q, const Qs_JobEntry* entry) {
    ca_mutex_lock(q->mutex);
    uint32_t next = (q->head + 1) % QS_JOB_QUEUE_CAP;
    if (next == q->tail) {
        ca_mutex_unlock(q->mutex);
        return false;
    }
    q->entries[q->head] = *entry;
    q->head = next;
    ca_condvar_signal(q->cond);
    ca_mutex_unlock(q->mutex);
    return true;
}

static bool queue_pop(Qs_JobQueue* q, Qs_JobEntry* out) {
    ca_mutex_lock(q->mutex);
    if (q->tail == q->head) {
        ca_mutex_unlock(q->mutex);
        return false;
    }
    *out = q->entries[q->tail];
    q->tail = (q->tail + 1) % QS_JOB_QUEUE_CAP;
    ca_mutex_unlock(q->mutex);
    return true;
}

/* ── Atomic counter helpers ─────────────────────────────────── */

#ifdef _WIN32
#include <intrin.h>
static void counter_increment(Qs_JobCounter* c) {
    _InterlockedIncrement(&c->value);
}
static void counter_decrement_and_notify(Qs_JobCounter* c) {
    if (_InterlockedDecrement(&c->value) == 0) {
        ca_mutex_lock(c->mutex);
        ca_condvar_broadcast(c->cond);
        ca_mutex_unlock(c->mutex);
    }
}
#else
static void counter_increment(Qs_JobCounter* c) {
    __sync_fetch_and_add(&c->value, 1);
}
static void counter_decrement_and_notify(Qs_JobCounter* c) {
    if (__sync_sub_and_fetch(&c->value, 1) == 0) {
        ca_mutex_lock(c->mutex);
        ca_condvar_broadcast(c->cond);
        ca_mutex_unlock(c->mutex);
    }
}
#endif

/* ── Execute one job ────────────────────────────────────────── */

static void execute_job(const Qs_JobEntry* entry) {
    entry->fn(entry->data);
    if (entry->counter) {
        counter_decrement_and_notify(entry->counter);
    }
}

/* ── Worker thread ──────────────────────────────────────────── */

static void* worker_fn(void* arg) {
    Qs_JobSystem* sys = (Qs_JobSystem*)arg;
    Qs_JobEntry entry;

    while (sys->running) {
        if (queue_pop(&sys->queue, &entry)) {
            execute_job(&entry);
        } else {
            ca_mutex_lock(sys->queue.mutex);
            if (sys->running && sys->queue.tail == sys->queue.head) {
                ca_condvar_wait(sys->queue.cond, sys->queue.mutex);
            }
            ca_mutex_unlock(sys->queue.mutex);
        }
    }

    /* Drain remaining jobs on shutdown */
    while (queue_pop(&sys->queue, &entry)) {
        execute_job(&entry);
    }

    return NULL;
}

/* ── CPU count ──────────────────────────────────────────────── */

static uint32_t get_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (uint32_t)si.dwNumberOfProcessors;
#elif defined(__APPLE__)
    int count = 0;
    size_t size = sizeof(count);
    if (sysctlbyname("hw.logicalcpu", &count, &size, NULL, 0) == 0 && count > 0)
        return (uint32_t)count;
    return 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (uint32_t)n : 1;
#endif
}

/* ── Public API ─────────────────────────────────────────────── */

Qs_JobSystem* qs_job_system_create(const Qs_JobSystemDesc* desc) {
    Qs_JobSystem* sys = calloc(1, sizeof(Qs_JobSystem));
    if (!sys) return NULL;

    sys->queue.mutex = ca_mutex_create();
    sys->queue.cond  = ca_condvar_create();
    if (!sys->queue.mutex || !sys->queue.cond) {
        ca_condvar_destroy(sys->queue.cond);
        ca_mutex_destroy(sys->queue.mutex);
        free(sys);
        return NULL;
    }

    uint32_t n = (desc && desc->num_threads > 0)
                 ? desc->num_threads
                 : get_cpu_count() - 1;
    if (n < 1) n = 1;
    sys->num_threads = n;

    sys->threads = calloc(n, sizeof(Ca_Thread*));
    if (!sys->threads) {
        ca_condvar_destroy(sys->queue.cond);
        ca_mutex_destroy(sys->queue.mutex);
        free(sys);
        return NULL;
    }

    sys->running = 1;
    for (uint32_t i = 0; i < n; ++i) {
        sys->threads[i] = ca_thread_create(worker_fn, sys);
    }

    return sys;
}

void qs_job_system_destroy(Qs_JobSystem* sys) {
    if (!sys) return;

    sys->running = 0;

    /* Wake all workers so they can exit */
    ca_mutex_lock(sys->queue.mutex);
    ca_condvar_broadcast(sys->queue.cond);
    ca_mutex_unlock(sys->queue.mutex);

    for (uint32_t i = 0; i < sys->num_threads; ++i) {
        ca_thread_join(sys->threads[i]);
    }

    free(sys->threads);
    ca_condvar_destroy(sys->queue.cond);
    ca_mutex_destroy(sys->queue.mutex);
    free(sys);
}

Qs_JobCounter* qs_job_counter_create(Qs_JobSystem* system) {
    (void)system;
    Qs_JobCounter* c = calloc(1, sizeof(Qs_JobCounter));
    if (!c) return NULL;
    c->mutex = ca_mutex_create();
    c->cond  = ca_condvar_create();
    if (!c->mutex || !c->cond) {
        ca_condvar_destroy(c->cond);
        ca_mutex_destroy(c->mutex);
        free(c);
        return NULL;
    }
    return c;
}

void qs_job_counter_destroy(Qs_JobSystem* system, Qs_JobCounter* counter) {
    (void)system;
    if (!counter) return;
    ca_condvar_destroy(counter->cond);
    ca_mutex_destroy(counter->mutex);
    free(counter);
}

void qs_job_dispatch(Qs_JobSystem* sys, const Qs_JobDesc* job,
                     Qs_JobCounter* counter) {
    if (!sys || !job || !job->fn) return;

    if (counter) counter_increment(counter);

    Qs_JobEntry entry = {
        .fn      = job->fn,
        .data    = job->data,
        .counter = counter,
    };
    queue_push(&sys->queue, &entry);
}

void qs_job_dispatch_batch(Qs_JobSystem* sys, const Qs_JobDesc* jobs,
                           uint32_t count, Qs_JobCounter* counter) {
    if (!sys || !jobs) return;

    for (uint32_t i = 0; i < count; ++i) {
        qs_job_dispatch(sys, &jobs[i], counter);
    }
}

void qs_job_wait(Qs_JobSystem* sys, Qs_JobCounter* counter) {
    if (!sys || !counter) return;

    Qs_JobEntry entry;
    while (counter->value > 0) {
        if (queue_pop(&sys->queue, &entry)) {
            execute_job(&entry);
        } else {
            ca_mutex_lock(counter->mutex);
            if (counter->value > 0) {
                ca_condvar_wait(counter->cond, counter->mutex);
            }
            ca_mutex_unlock(counter->mutex);
        }
    }
}

uint32_t qs_job_system_thread_count(const Qs_JobSystem* sys) {
    return sys ? sys->num_threads : 0;
}
