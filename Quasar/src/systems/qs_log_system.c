#include "qs_log.h"
#include "qs_system.h"
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
    state->entries     = calloc(state->capacity, sizeof(Qs_LogEntry));
    state->storage     = calloc(state->capacity, sizeof(Qs_LogStorage));
    if (!state->entries || !state->storage) return false;

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
        free(state->storage[i].text);

    free(state->entries);
    free(state->storage);
    state->entries = NULL;
    state->storage = NULL;
    state->count   = 0;

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

    /* Format message */
    char buf[QS_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    double elapsed = get_time_sec() - g_log->start_time;

    /* Grow arrays if needed */
    if (g_log->count == g_log->capacity) {
        uint32_t new_cap = g_log->capacity * 2;
        Qs_LogEntry   *new_entries = realloc(g_log->entries, new_cap * sizeof(Qs_LogEntry));
        Qs_LogStorage *new_storage = realloc(g_log->storage, new_cap * sizeof(Qs_LogStorage));
        if (!new_entries || !new_storage) return;
        g_log->entries  = new_entries;
        g_log->storage  = new_storage;
        g_log->capacity = new_cap;
    }

    /* Store message */
    size_t msg_len = strlen(buf);
    char *text = malloc(msg_len + 1);
    if (!text) return;
    memcpy(text, buf, msg_len + 1);

    uint32_t idx = g_log->count++;
    g_log->storage[idx].text = text;
    g_log->entries[idx] = (Qs_LogEntry){
        .level     = level,
        .timestamp = elapsed,
        .message   = text,
    };

    /* Print to stdout with color */
    char ts[16];
    format_timestamp(elapsed, ts, sizeof(ts));
    printf("%s[%s] [%s] %s\033[0m\n", g_level_colors[level], ts, g_level_labels[level], buf);

    /* Auto-flush to file at threshold */
    g_log->unflushed++;
    if (g_log->unflushed >= QS_LOG_FLUSH_THRESH)
        flush_to_file(g_log);

    /* Notify listener */
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
    if (out_count) *out_count = g_log->count;
    return g_log->entries;
}

void qs_log_set_level(Qs_LogLevel min_level)
{
    if (g_log) g_log->min_level = min_level;
}

void qs_log_flush(void)
{
    if (g_log) flush_to_file(g_log);
}

void qs_log_set_listener(Qs_LogListenerFn fn, void *userdata)
{
    if (g_log) {
        g_log->listener      = fn;
        g_log->listener_data = userdata;
    }
}
