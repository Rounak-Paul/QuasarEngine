#ifndef QS_LOG_H
#define QS_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include "qs_api.h"

/// Log severity levels.
typedef enum Qs_LogLevel {
    QS_LOG_DEBUG = 0,
    QS_LOG_TRACE,
    QS_LOG_INFO,
    QS_LOG_WARN,
    QS_LOG_ERROR,
    QS_LOG_FATAL,
    QS_LOG_LEVEL_COUNT
} Qs_LogLevel;

/// A single log entry.
typedef struct Qs_LogEntry {
    Qs_LogLevel level;
    double      timestamp;      ///< Seconds since engine start.
    const char *message;        ///< Null-terminated log text.
} Qs_LogEntry;

/// Emits a formatted log message at the given severity.
QS_API void qs_log(Qs_LogLevel level, const char *fmt, ...);

/// Returns the severity label string for a level (e.g. "INFO").
QS_API const char *qs_log_level_str(Qs_LogLevel level);

/// Returns all buffered log entries and their count.
/// The returned pointer is valid until the next call to qs_log() or shutdown.
QS_API const Qs_LogEntry *qs_log_entries(uint32_t *out_count);

/// Sets the minimum level that gets recorded. Messages below this are discarded.
QS_API void qs_log_set_level(Qs_LogLevel min_level);

/// Forces an immediate flush of the log buffer to disk.
QS_API void qs_log_flush(void);

/// Callback invoked whenever a log entry is appended.
typedef void (*Qs_LogListenerFn)(void *userdata);

/// Sets a listener called after each log entry. Pass NULL to clear.
QS_API void qs_log_set_listener(Qs_LogListenerFn fn, void *userdata);

/* ── Convenience macros ─────────────────────────────────────── */

#define QS_LOG_DEBUG(fmt, ...) qs_log(QS_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define QS_LOG_TRACE(fmt, ...) qs_log(QS_LOG_TRACE, fmt, ##__VA_ARGS__)
#define QS_LOG_INFO(fmt, ...)  qs_log(QS_LOG_INFO,  fmt, ##__VA_ARGS__)
#define QS_LOG_WARN(fmt, ...)  qs_log(QS_LOG_WARN,  fmt, ##__VA_ARGS__)
#define QS_LOG_ERROR(fmt, ...) qs_log(QS_LOG_ERROR, fmt, ##__VA_ARGS__)
#define QS_LOG_FATAL(fmt, ...) qs_log(QS_LOG_FATAL, fmt, ##__VA_ARGS__)

#endif
