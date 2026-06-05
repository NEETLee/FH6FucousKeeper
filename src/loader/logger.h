#ifndef FOCUSKEEPER_LOGGER_H
#define FOCUSKEEPER_LOGGER_H

#include <windows.h>

/*
 * Logger - Observer Pattern
 *
 * Centralized logging system that dispatches log entries to multiple
 * sinks (GUI text control, file, debug output). Observers register
 * via callback to receive log messages.
 */

/* Log severity levels */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

/* Log entry structure */
typedef struct {
    LogLevel    level;
    SYSTEMTIME  timestamp;
    WCHAR       message[1024];
} LogEntry;

/* Observer callback type */
typedef void (*LogCallback)(const LogEntry *entry, void *user_data);

/* ─── Public API ──────────────────────────────────────────────────── */

/* Initialize logger (optionally with file output) */
BOOL    Logger_Init(const WCHAR *log_file_path);

/* Shutdown logger, flush and close file */
void    Logger_Shutdown(void);

/* Log a message at the specified level */
void    Logger_Log(LogLevel level, const WCHAR *fmt, ...);

/* Convenience macros */
#define LOG_D(fmt, ...) Logger_Log(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) Logger_Log(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) Logger_Log(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) Logger_Log(LOG_ERROR, fmt, ##__VA_ARGS__)

/* Register/unregister observer callbacks */
BOOL    Logger_AddCallback(LogCallback callback, void *user_data);
void    Logger_RemoveCallback(LogCallback callback);

/* Enable/disable file logging at runtime */
void    Logger_SetFileEnabled(BOOL enabled);

/* Get level name string */
const WCHAR* Logger_LevelName(LogLevel level);

#endif /* FOCUSKEEPER_LOGGER_H */
