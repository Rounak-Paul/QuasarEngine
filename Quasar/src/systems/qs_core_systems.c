/* qs_core_systems.c — Log, Event, Job, and Input systems (consolidated). */

#include "qs_log.h"
#include "qs_system.h"
#include <causality.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_sec(void) {
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}
#endif

#define QS_LOG_INITIAL_CAP  1024
#define QS_LOG_FLUSH_THRESH 256
#define QS_LOG_MSG_MAX      1024
#define QS_LOG_FILE_NAME    "quasar.log"

typedef struct {
    char *text;
} Qs_LogStorage;

typedef struct {
    Qs_LogEntry   *entries;
    Qs_LogStorage *storage;
    uint32_t       count;
    uint32_t       capacity;
    uint32_t       unflushed;
    Qs_LogLevel    min_level;
    double         start_time;
    FILE          *file;
    Qs_LogListenerFn listener;
    void            *listener_data;
    Ca_Mutex        *mutex;   /* protects entries/storage/count/unflushed */
} Qs_LogState;

static Qs_LogState *g_log = NULL;

static const char *g_level_labels[QS_LOG_LEVEL_COUNT] = {
    "DEBUG", "TRACE", "INFO ", "WARN ", "ERROR", "FATAL"
};

static const char *g_level_colors[QS_LOG_LEVEL_COUNT] = {
    "\033[90m",   /* DEBUG — gray     */
    "\033[36m",   /* TRACE — cyan     */
    "\033[32m",   /* INFO  — green    */
    "\033[33m",   /* WARN  — yellow   */
    "\033[31m",   /* ERROR — red      */
    "\033[35m",   /* FATAL — magenta  */
};

static void format_timestamp(double elapsed, char *buf, size_t len)
{
    int hours   = (int)(elapsed / 3600.0);
    int minutes = (int)(elapsed / 60.0) % 60;
    int seconds = (int)elapsed % 60;
    int millis  = (int)((elapsed - (int)elapsed) * 1000.0);
    snprintf(buf, len, "%02d:%02d:%02d.%03d", hours, minutes, seconds, millis);
}

static void flush_to_file(Qs_LogState *state)
{
    if (!state->file || state->unflushed == 0) return;

    uint32_t start = state->count - state->unflushed;
    for (uint32_t i = start; i < state->count; i++) {
        Qs_LogEntry *e = &state->entries[i];
        char ts[16];
        format_timestamp(e->timestamp, ts, sizeof(ts));
        fprintf(state->file, "[%s] [%s] %s\n",
                ts, g_level_labels[e->level], e->message);
    }
    fflush(state->file);
    state->unflushed = 0;
}

static bool log_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_LogState *state = (Qs_LogState *)qs_system_data(system);

    state->capacity   = QS_LOG_INITIAL_CAP;
    state->entries     = qs_calloc(state->capacity, sizeof(Qs_LogEntry), QS_MEM_LOG);
    state->storage     = qs_calloc(state->capacity, sizeof(Qs_LogStorage), QS_MEM_LOG);
    if (!state->entries || !state->storage) return false;

    state->mutex = ca_mutex_create();
    if (!state->mutex) { qs_free(state->entries); qs_free(state->storage); return false; }

    state->min_level  = QS_LOG_DEBUG;
    state->start_time = get_time_sec();
    state->file       = fopen(QS_LOG_FILE_NAME, "w");

    g_log = state;

    if (state->file) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char date_buf[64];
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", t);
        fprintf(state->file, "=== Quasar Engine Log — %s ===\n\n", date_buf);
        fflush(state->file);
    }

    return true;
}

static void log_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_LogState *state = (Qs_LogState *)qs_system_data(system);

    flush_to_file(state);

    if (state->file) {
        fprintf(state->file, "\n=== Log closed ===\n");
        fclose(state->file);
        state->file = NULL;
    }

    for (uint32_t i = 0; i < state->count; i++)
        qs_free(state->storage[i].text);

    qs_free(state->entries);
    qs_free(state->storage);
    state->entries = NULL;
    state->storage = NULL;
    state->count   = 0;

    if (state->mutex) { ca_mutex_destroy(state->mutex); state->mutex = NULL; }

    g_log = NULL;
}

Qs_SystemDesc qs_log_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Log",
        .data_size = sizeof(Qs_LogState),
        .init      = log_system_init,
        .shutdown  = log_system_shutdown,
    };
}

void qs_log(Qs_LogLevel level, const char *fmt, ...)
{
    if (!g_log) {
        /* Fallback before log system is up */
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n");
        return;
    }

    if (level < g_log->min_level) return;

    /* Format message (outside lock — uses only local stack) */
    char buf[QS_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    double elapsed = get_time_sec() - g_log->start_time;

    /* Print to stdout first (outside lock) so interactive consoles stay
       responsive even if the storage lock is briefly contended. */
    char ts[16];
    format_timestamp(elapsed, ts, sizeof(ts));
    printf("%s[%s] [%s] %s\033[0m\n",
           g_level_colors[level], ts, g_level_labels[level], buf);

    if (g_log->mutex) ca_mutex_lock(g_log->mutex);

    /* Grow arrays if needed.
       Update each pointer immediately after its own realloc so that
       g_log->entries is never left pointing at freed memory if the
       second realloc fails (classic double-realloc use-after-free). */
    if (g_log->count == g_log->capacity) {
        uint32_t new_cap = g_log->capacity * 2;
        Qs_LogEntry *ne = qs_realloc(g_log->entries, new_cap * sizeof(Qs_LogEntry), QS_MEM_LOG);
        if (!ne) { if (g_log->mutex) ca_mutex_unlock(g_log->mutex); return; }
        g_log->entries = ne;   /* update immediately — pointer is now valid */
        Qs_LogStorage *ns = qs_realloc(g_log->storage, new_cap * sizeof(Qs_LogStorage), QS_MEM_LOG);
        if (!ns) { if (g_log->mutex) ca_mutex_unlock(g_log->mutex); return; }
        g_log->storage  = ns;
        g_log->capacity = new_cap;
    }

    /* Store message */
    size_t msg_len = strlen(buf);
    char *text = qs_malloc(msg_len + 1, QS_MEM_LOG);
    if (!text) {
        if (g_log->mutex) ca_mutex_unlock(g_log->mutex);
        return;
    }
    memcpy(text, buf, msg_len + 1);

    uint32_t idx = g_log->count++;
    g_log->storage[idx].text = text;
    g_log->entries[idx] = (Qs_LogEntry){
        .level     = level,
        .timestamp = elapsed,
        .message   = text,
    };

    /* Auto-flush to file at threshold */
    g_log->unflushed++;
    if (g_log->unflushed >= QS_LOG_FLUSH_THRESH)
        flush_to_file(g_log);

    if (g_log->mutex) ca_mutex_unlock(g_log->mutex);

    /* Notify listener (outside lock to avoid re-entrancy deadlock) */
    if (g_log->listener)
        g_log->listener(g_log->listener_data);
}

const char *qs_log_level_str(Qs_LogLevel level)
{
    if (level >= QS_LOG_LEVEL_COUNT) return "?????";
    return g_level_labels[level];
}

const Qs_LogEntry *qs_log_entries(uint32_t *out_count)
{
    if (!g_log) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    /* Lock so count and entries pointer are consistent w.r.t. concurrent
       worker-thread calls to qs_log() that may realloc the array. */
    if (g_log->mutex) ca_mutex_lock(g_log->mutex);
    uint32_t n           = g_log->count;
    const Qs_LogEntry *arr = g_log->entries;
    if (g_log->mutex) ca_mutex_unlock(g_log->mutex);
    if (out_count) *out_count = n;
    return arr;
}

void qs_log_clear(void)
{
    if (!g_log) return;

    Qs_LogListenerFn listener = NULL;
    void *listener_data = NULL;

    if (g_log->mutex) ca_mutex_lock(g_log->mutex);

    flush_to_file(g_log);

    for (uint32_t index = 0; index < g_log->count; index++) {
        qs_free(g_log->storage[index].text);
        g_log->storage[index].text = NULL;
        g_log->entries[index] = (Qs_LogEntry){0};
    }
    g_log->count = 0;
    g_log->unflushed = 0;
    listener = g_log->listener;
    listener_data = g_log->listener_data;

    if (g_log->mutex) ca_mutex_unlock(g_log->mutex);

    if (listener)
        listener(listener_data);
}

void qs_log_set_level(Qs_LogLevel min_level)
{
    if (!g_log) return;
    if (g_log->mutex) ca_mutex_lock(g_log->mutex);
    g_log->min_level = min_level;
    if (g_log->mutex) ca_mutex_unlock(g_log->mutex);
}

void qs_log_flush(void)
{
    if (!g_log) return;
    if (g_log->mutex) ca_mutex_lock(g_log->mutex);
    flush_to_file(g_log);
    if (g_log->mutex) ca_mutex_unlock(g_log->mutex);
}

void qs_log_set_listener(Qs_LogListenerFn fn, void *userdata)
{
    if (!g_log) return;
    if (g_log->mutex) ca_mutex_lock(g_log->mutex);
    g_log->listener      = fn;
    g_log->listener_data = userdata;
    if (g_log->mutex) ca_mutex_unlock(g_log->mutex);
}

/* ================================================================
   EVENT SYSTEM  (was qs_event_system.c)
   ================================================================ */

#include "qs_event.h"
#include "qs_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct Qs_Listener {
    Qs_EventId  id;
    Qs_EventFn  callback;
    void       *user_data;
    uint32_t    handle;
} Qs_Listener;

struct Qs_EventBus {
    Qs_Listener *listeners;
    uint32_t     count;
    uint32_t     capacity;
    uint32_t     next_handle;
};

#define QS_EVENT_INITIAL_CAP 32

static bool event_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_EventBus *bus = qs_system_data(system);

    bus->capacity    = QS_EVENT_INITIAL_CAP;
    bus->listeners   = qs_calloc(bus->capacity, sizeof(Qs_Listener), QS_MEM_EVENT);
    if (!bus->listeners) return false;
    bus->next_handle = 1;
    return true;
}

static void event_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_EventBus *bus = qs_system_data(system);
    qs_free(bus->listeners);
    bus->listeners = NULL;
    bus->count     = 0;
    bus->capacity  = 0;
}

Qs_SystemDesc qs_event_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Event",
        .data_size = sizeof(Qs_EventBus),
        .init      = event_system_init,
        .shutdown  = event_system_shutdown,
    };
}

uint32_t qs_event_subscribe(Qs_EventBus *bus, Qs_EventId id,
                            Qs_EventFn callback, void *user_data)
{
    if (!bus || !callback) return 0;

    if (bus->count == bus->capacity) {
        uint32_t new_cap = bus->capacity * 2;
        Qs_Listener *grown = qs_realloc(bus->listeners, new_cap * sizeof(Qs_Listener), QS_MEM_EVENT);
        if (!grown) return 0;
        bus->listeners = grown;
        bus->capacity  = new_cap;
    }

    uint32_t handle = bus->next_handle++;
    bus->listeners[bus->count++] = (Qs_Listener){
        .id        = id,
        .callback  = callback,
        .user_data = user_data,
        .handle    = handle,
    };
    return handle;
}

void qs_event_unsubscribe(Qs_EventBus *bus, uint32_t handle)
{
    if (!bus || handle == 0) return;

    for (uint32_t i = 0; i < bus->count; ++i) {
        if (bus->listeners[i].handle == handle) {
            bus->listeners[i] = bus->listeners[--bus->count];
            return;
        }
    }
}

void qs_event_fire(Qs_EventBus *bus, Qs_EventId id,
                   void *data, uint32_t data_size)
{
    if (!bus) return;

    Qs_Event event = {
        .id        = id,
        .data      = data,
        .data_size = data_size,
        .handled   = false,
    };

    for (uint32_t i = 0; i < bus->count; ++i) {
        if (bus->listeners[i].id == id) {
            if (bus->listeners[i].callback(&event, bus->listeners[i].user_data)) {
                event.handled = true;
                return;
            }
        }
    }
}

/* ================================================================
   JOB SYSTEM  (was qs_job_system.c)
   ================================================================ */

#include "qs_job.h"
#include "qs_log.h"
#include "qs_system.h"
#include <stdlib.h>

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

/* ── System callbacks ───────────────────────────────────────── */

static bool job_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_JobSystem **slot = (Qs_JobSystem **)qs_system_data(system);

    Qs_JobSystem *sys = qs_calloc(1, sizeof(Qs_JobSystem), QS_MEM_JOB);
    if (!sys) return false;

    sys->queue.mutex = ca_mutex_create();
    sys->queue.cond  = ca_condvar_create();
    if (!sys->queue.mutex || !sys->queue.cond) {
        ca_condvar_destroy(sys->queue.cond);
        ca_mutex_destroy(sys->queue.mutex);
        qs_free(sys);
        return false;
    }

    uint32_t n = get_cpu_count() - 1;
    if (n < 1) n = 1;
    sys->num_threads = n;

    sys->threads = qs_calloc(n, sizeof(Ca_Thread *), QS_MEM_JOB);
    if (!sys->threads) {
        ca_condvar_destroy(sys->queue.cond);
        ca_mutex_destroy(sys->queue.mutex);
        qs_free(sys);
        return false;
    }

    sys->running = 1;
    for (uint32_t i = 0; i < n; ++i)
        sys->threads[i] = ca_thread_create(worker_fn, sys);

    *slot = sys;
    QS_LOG_DEBUG("%u worker threads spawned", n);
    return true;
}

static void job_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_JobSystem **slot = (Qs_JobSystem **)qs_system_data(system);
    Qs_JobSystem *sys = *slot;
    if (!sys) return;

    sys->running = 0;

    ca_mutex_lock(sys->queue.mutex);
    ca_condvar_broadcast(sys->queue.cond);
    ca_mutex_unlock(sys->queue.mutex);

    for (uint32_t i = 0; i < sys->num_threads; ++i)
        ca_thread_join(sys->threads[i]);

    qs_free(sys->threads);
    ca_condvar_destroy(sys->queue.cond);
    ca_mutex_destroy(sys->queue.mutex);
    qs_free(sys);
    *slot = NULL;
}

Qs_SystemDesc qs_job_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Job",
        .data_size = sizeof(Qs_JobSystem *),
        .init      = job_system_init,
        .shutdown  = job_system_shutdown,
    };
}

/* ── Public API ─────────────────────────────────────────────── */

Qs_JobCounter* qs_job_counter_create(Qs_JobSystem* system) {
    (void)system;
    Qs_JobCounter* c = qs_calloc(1, sizeof(Qs_JobCounter), QS_MEM_JOB);
    if (!c) return NULL;
    c->mutex = ca_mutex_create();
    c->cond  = ca_condvar_create();
    if (!c->mutex || !c->cond) {
        ca_condvar_destroy(c->cond);
        ca_mutex_destroy(c->mutex);
        qs_free(c);
        return NULL;
    }
    return c;
}

void qs_job_counter_destroy(Qs_JobSystem* system, Qs_JobCounter* counter) {
    (void)system;
    if (!counter) return;
    ca_condvar_destroy(counter->cond);
    ca_mutex_destroy(counter->mutex);
    qs_free(counter);
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
    if (!queue_push(&sys->queue, &entry)) {
        /* Queue full: roll back the counter increment to prevent qs_job_wait
           from hanging forever waiting on a job that was never queued. */
        QS_LOG_WARN("Job queue full — job dropped (increase QS_JOB_QUEUE_CAP)");
        if (counter) counter_decrement_and_notify(counter);
    }
}

void qs_job_dispatch_batch(Qs_JobSystem* sys, const Qs_JobDesc* jobs,
                           uint32_t count, Qs_JobCounter* counter) {
    if (!sys || !jobs) return;

    for (uint32_t i = 0; i < count; ++i)
        qs_job_dispatch(sys, &jobs[i], counter);
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

/* ================================================================
   INPUT SYSTEM  (was qs_input_system.c)
   ================================================================ */

#include "qs_input.h"
#include "qs_system.h"
#include "qs_log.h"
#include <string.h>

typedef struct {
    bool  current[QS_KEY_MAX];
    bool  previous[QS_KEY_MAX];

    /* Mouse state */
    bool  mouse_current[QS_MOUSE_BUTTON_COUNT];
    bool  mouse_previous[QS_MOUSE_BUTTON_COUNT];
    float mouse_x,       mouse_y;        /* current absolute position */
    float mouse_delta_x, mouse_delta_y;  /* accumulated movement this frame */
    float scroll_dx,     scroll_dy;      /* accumulated scroll this frame */
    bool  mouse_initialized;             /* false until first cursor event */
} Qs_InputState;

static Qs_InputState *g_input = NULL;

static bool input_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    g_input = (Qs_InputState *)qs_system_data(system);
    memset(g_input->current,        0, sizeof(g_input->current));
    memset(g_input->previous,       0, sizeof(g_input->previous));
    memset(g_input->mouse_current,  0, sizeof(g_input->mouse_current));
    memset(g_input->mouse_previous, 0, sizeof(g_input->mouse_previous));
    g_input->mouse_x = g_input->mouse_y = 0.0f;
    g_input->mouse_delta_x = g_input->mouse_delta_y = 0.0f;
    g_input->scroll_dx = g_input->scroll_dy = 0.0f;
    g_input->mouse_initialized = false;
    return true;
}

static void input_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)system;
    (void)engine;
    g_input = NULL;
}

static void input_system_update(Qs_System *system, Qs_Engine *engine, float dt)
{
    (void)system;
    (void)engine;
    (void)dt;
    /* Snapshot is done in qs_input_end_frame() so that on_frame consumers
       can read single-frame transitions (pressed / released). */
}

Qs_SystemDesc qs_input_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Input",
        .data_size = sizeof(Qs_InputState),
        .init      = input_system_init,
        .shutdown  = input_system_shutdown,
        .update    = input_system_update,
    };
}

void qs_input_key_event(Qs_Key key, Qs_KeyAction action, int mods)
{
    if (!g_input) return;
    if (key < 0 || key >= QS_KEY_MAX) return;
    (void)mods;

    switch (action) {
    case QS_KEY_PRESS:   g_input->current[key] = true;  break;
    case QS_KEY_RELEASE: g_input->current[key] = false; break;
    case QS_KEY_REPEAT:  break;
    }
}

bool qs_input_key_down(Qs_Key key)
{
    if (!g_input || key < 0 || key >= QS_KEY_MAX) return false;
    return g_input->current[key];
}

bool qs_input_key_pressed(Qs_Key key)
{
    if (!g_input || key < 0 || key >= QS_KEY_MAX) return false;
    return g_input->current[key] && !g_input->previous[key];
}

bool qs_input_key_released(Qs_Key key)
{
    if (!g_input || key < 0 || key >= QS_KEY_MAX) return false;
    return !g_input->current[key] && g_input->previous[key];
}

/* ================================================================
   MOUSE IMPLEMENTATION
   ================================================================ */

void qs_input_mouse_button_event(Qs_MouseButton button, int action)
{
    if (!g_input || button < 0 || (int)button >= QS_MOUSE_BUTTON_COUNT) return;
    g_input->mouse_current[button] = (action != 0);
}

void qs_input_mouse_pos_event(double x, double y)
{
    if (!g_input) return;
    if (!g_input->mouse_initialized) {
        /* First event: initialise position without producing a spurious delta. */
        g_input->mouse_x = (float)x;
        g_input->mouse_y = (float)y;
        g_input->mouse_initialized = true;
        return;
    }
    g_input->mouse_delta_x += (float)x - g_input->mouse_x;
    g_input->mouse_delta_y += (float)y - g_input->mouse_y;
    g_input->mouse_x = (float)x;
    g_input->mouse_y = (float)y;
}

void qs_input_mouse_scroll_event(double dx, double dy)
{
    if (!g_input) return;
    g_input->scroll_dx += (float)dx;
    g_input->scroll_dy += (float)dy;
}

bool qs_input_mouse_down(Qs_MouseButton button)
{
    if (!g_input || button < 0 || (int)button >= QS_MOUSE_BUTTON_COUNT) return false;
    return g_input->mouse_current[button];
}

bool qs_input_mouse_pressed(Qs_MouseButton button)
{
    if (!g_input || button < 0 || (int)button >= QS_MOUSE_BUTTON_COUNT) return false;
    return g_input->mouse_current[button] && !g_input->mouse_previous[button];
}

bool qs_input_mouse_released(Qs_MouseButton button)
{
    if (!g_input || button < 0 || (int)button >= QS_MOUSE_BUTTON_COUNT) return false;
    return !g_input->mouse_current[button] && g_input->mouse_previous[button];
}

void qs_input_mouse_pos(float *out_x, float *out_y)
{
    if (out_x) *out_x = g_input ? g_input->mouse_x : 0.0f;
    if (out_y) *out_y = g_input ? g_input->mouse_y : 0.0f;
}

void qs_input_mouse_delta(float *out_dx, float *out_dy)
{
    if (out_dx) *out_dx = g_input ? g_input->mouse_delta_x : 0.0f;
    if (out_dy) *out_dy = g_input ? g_input->mouse_delta_y : 0.0f;
}

void qs_input_end_frame(void)
{
    if (!g_input) return;
    /* Snapshot key and mouse-button states so that key_pressed / key_released
       detect single-frame transitions on the NEXT frame. */
    memcpy(g_input->previous,       g_input->current,       sizeof(g_input->current));
    memcpy(g_input->mouse_previous, g_input->mouse_current, sizeof(g_input->mouse_current));
    g_input->mouse_delta_x = 0.0f;
    g_input->mouse_delta_y = 0.0f;
    g_input->scroll_dx     = 0.0f;
    g_input->scroll_dy     = 0.0f;
}

void qs_input_mouse_scroll(float *out_dx, float *out_dy)
{
    if (out_dx) *out_dx = g_input ? g_input->scroll_dx : 0.0f;
    if (out_dy) *out_dy = g_input ? g_input->scroll_dy : 0.0f;
}

const char *qs_key_name(Qs_Key key)
{
    switch (key) {
    case QS_KEY_SPACE:         return "Space";
    case QS_KEY_APOSTROPHE:    return "'";
    case QS_KEY_COMMA:         return ",";
    case QS_KEY_MINUS:         return "-";
    case QS_KEY_PERIOD:        return ".";
    case QS_KEY_SLASH:         return "/";
    case QS_KEY_0: return "0"; case QS_KEY_1: return "1";
    case QS_KEY_2: return "2"; case QS_KEY_3: return "3";
    case QS_KEY_4: return "4"; case QS_KEY_5: return "5";
    case QS_KEY_6: return "6"; case QS_KEY_7: return "7";
    case QS_KEY_8: return "8"; case QS_KEY_9: return "9";
    case QS_KEY_SEMICOLON:     return ";";
    case QS_KEY_EQUAL:         return "=";
    case QS_KEY_A: return "A"; case QS_KEY_B: return "B";
    case QS_KEY_C: return "C"; case QS_KEY_D: return "D";
    case QS_KEY_E: return "E"; case QS_KEY_F: return "F";
    case QS_KEY_G: return "G"; case QS_KEY_H: return "H";
    case QS_KEY_I: return "I"; case QS_KEY_J: return "J";
    case QS_KEY_K: return "K"; case QS_KEY_L: return "L";
    case QS_KEY_M: return "M"; case QS_KEY_N: return "N";
    case QS_KEY_O: return "O"; case QS_KEY_P: return "P";
    case QS_KEY_Q: return "Q"; case QS_KEY_R: return "R";
    case QS_KEY_S: return "S"; case QS_KEY_T: return "T";
    case QS_KEY_U: return "U"; case QS_KEY_V: return "V";
    case QS_KEY_W: return "W"; case QS_KEY_X: return "X";
    case QS_KEY_Y: return "Y"; case QS_KEY_Z: return "Z";
    case QS_KEY_LEFT_BRACKET:  return "[";
    case QS_KEY_BACKSLASH:     return "\\";
    case QS_KEY_RIGHT_BRACKET: return "]";
    case QS_KEY_GRAVE_ACCENT:  return "`";
    case QS_KEY_ESCAPE:        return "Escape";
    case QS_KEY_ENTER:         return "Enter";
    case QS_KEY_TAB:           return "Tab";
    case QS_KEY_BACKSPACE:     return "Backspace";
    case QS_KEY_INSERT:        return "Insert";
    case QS_KEY_DELETE:        return "Delete";
    case QS_KEY_RIGHT:         return "Right";
    case QS_KEY_LEFT:          return "Left";
    case QS_KEY_DOWN:          return "Down";
    case QS_KEY_UP:            return "Up";
    case QS_KEY_PAGE_UP:       return "PageUp";
    case QS_KEY_PAGE_DOWN:     return "PageDown";
    case QS_KEY_HOME:          return "Home";
    case QS_KEY_END:           return "End";
    case QS_KEY_CAPS_LOCK:     return "CapsLock";
    case QS_KEY_SCROLL_LOCK:   return "ScrollLock";
    case QS_KEY_NUM_LOCK:      return "NumLock";
    case QS_KEY_PRINT_SCREEN:  return "PrintScreen";
    case QS_KEY_PAUSE:         return "Pause";
    case QS_KEY_F1:  return "F1";  case QS_KEY_F2:  return "F2";
    case QS_KEY_F3:  return "F3";  case QS_KEY_F4:  return "F4";
    case QS_KEY_F5:  return "F5";  case QS_KEY_F6:  return "F6";
    case QS_KEY_F7:  return "F7";  case QS_KEY_F8:  return "F8";
    case QS_KEY_F9:  return "F9";  case QS_KEY_F10: return "F10";
    case QS_KEY_F11: return "F11"; case QS_KEY_F12: return "F12";
    case QS_KEY_KP_0: return "KP0"; case QS_KEY_KP_1: return "KP1";
    case QS_KEY_KP_2: return "KP2"; case QS_KEY_KP_3: return "KP3";
    case QS_KEY_KP_4: return "KP4"; case QS_KEY_KP_5: return "KP5";
    case QS_KEY_KP_6: return "KP6"; case QS_KEY_KP_7: return "KP7";
    case QS_KEY_KP_8: return "KP8"; case QS_KEY_KP_9: return "KP9";
    case QS_KEY_KP_DECIMAL:    return "KP.";
    case QS_KEY_KP_DIVIDE:     return "KP/";
    case QS_KEY_KP_MULTIPLY:   return "KP*";
    case QS_KEY_KP_SUBTRACT:   return "KP-";
    case QS_KEY_KP_ADD:        return "KP+";
    case QS_KEY_KP_ENTER:      return "KPEnter";
    case QS_KEY_LEFT_SHIFT:    return "LShift";
    case QS_KEY_LEFT_CONTROL:  return "LCtrl";
    case QS_KEY_LEFT_ALT:      return "LAlt";
    case QS_KEY_LEFT_SUPER:    return "LSuper";
    case QS_KEY_RIGHT_SHIFT:   return "RShift";
    case QS_KEY_RIGHT_CONTROL: return "RCtrl";
    case QS_KEY_RIGHT_ALT:     return "RAlt";
    case QS_KEY_RIGHT_SUPER:   return "RSuper";
    case QS_KEY_MENU:          return "Menu";
    default:                   return "Unknown";
    }
}
