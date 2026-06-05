/*
 * logger.c - Centralized Logging (Observer Pattern)
 *
 * Supports multiple output sinks via registered callbacks:
 * - GUI: Updates an edit control in the log tab
 * - File: Writes to a rotating log file
 * - Debug: OutputDebugString for debugger attachment
 *
 * Thread-safe via critical section.
 */

#include "logger.h"
#include <stdio.h>

#define MAX_LOG_CALLBACKS 8

/* ─── Module State ────────────────────────────────────────────────── */
typedef struct {
    LogCallback callback;
    void       *user_data;
} CallbackEntry;

static struct {
    CallbackEntry       callbacks[MAX_LOG_CALLBACKS];
    int                 callback_count;
    CRITICAL_SECTION    cs;
    HANDLE              log_file;
    BOOL                file_enabled;
    BOOL                initialized;
} s_logger = {0};

/* ─── Internal Helpers ────────────────────────────────────────────── */

static void WriteToFile(const LogEntry *entry)
{
    WCHAR line[1200];
    DWORD written;
    int len;

    if (!s_logger.log_file || s_logger.log_file == INVALID_HANDLE_VALUE) return;
    if (!s_logger.file_enabled) return;

    len = _snwprintf(line, 1199,
        L"[%02d:%02d:%02d.%03d] [%s] %s\r\n",
        entry->timestamp.wHour, entry->timestamp.wMinute,
        entry->timestamp.wSecond, entry->timestamp.wMilliseconds,
        Logger_LevelName(entry->level),
        entry->message);

    if (len > 0) {
        /* Write as UTF-8 for compatibility */
        char utf8[2400];
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, line, len, utf8, sizeof(utf8), NULL, NULL);
        if (utf8_len > 0) {
            WriteFile(s_logger.log_file, utf8, utf8_len, &written, NULL);
        }
    }
}

static void NotifyCallbacks(const LogEntry *entry)
{
    for (int i = 0; i < s_logger.callback_count; i++) {
        if (s_logger.callbacks[i].callback) {
            s_logger.callbacks[i].callback(entry, s_logger.callbacks[i].user_data);
        }
    }
}

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL Logger_Init(const WCHAR *log_file_path)
{
    if (s_logger.initialized) return TRUE;

    InitializeCriticalSection(&s_logger.cs);
    s_logger.callback_count = 0;
    s_logger.file_enabled = TRUE;
    s_logger.log_file = INVALID_HANDLE_VALUE;

    if (log_file_path) {
        s_logger.log_file = CreateFileW(
            log_file_path,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (s_logger.log_file == INVALID_HANDLE_VALUE) {
            /* Non-fatal: continue without file logging */
            s_logger.file_enabled = FALSE;
        } else {
            /* Write UTF-8 BOM */
            DWORD written;
            unsigned char bom[] = {0xEF, 0xBB, 0xBF};
            WriteFile(s_logger.log_file, bom, 3, &written, NULL);
        }
    } else {
        s_logger.file_enabled = FALSE;
    }

    s_logger.initialized = TRUE;
    return TRUE;
}

void Logger_Shutdown(void)
{
    if (!s_logger.initialized) return;

    EnterCriticalSection(&s_logger.cs);

    if (s_logger.log_file && s_logger.log_file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(s_logger.log_file);
        CloseHandle(s_logger.log_file);
        s_logger.log_file = INVALID_HANDLE_VALUE;
    }

    s_logger.callback_count = 0;
    s_logger.initialized = FALSE;

    LeaveCriticalSection(&s_logger.cs);
    DeleteCriticalSection(&s_logger.cs);
}

void Logger_Log(LogLevel level, const WCHAR *fmt, ...)
{
    LogEntry entry;
    va_list args;

    if (!s_logger.initialized) return;

    GetLocalTime(&entry.timestamp);
    entry.level = level;

    va_start(args, fmt);
    _vsnwprintf(entry.message, 1023, fmt, args);
    entry.message[1023] = L'\0';
    va_end(args);

    /* Output to debugger */
    OutputDebugStringW(entry.message);
    OutputDebugStringW(L"\n");

    EnterCriticalSection(&s_logger.cs);

    WriteToFile(&entry);
    NotifyCallbacks(&entry);

    LeaveCriticalSection(&s_logger.cs);
}

BOOL Logger_AddCallback(LogCallback callback, void *user_data)
{
    if (!callback) return FALSE;
    if (s_logger.callback_count >= MAX_LOG_CALLBACKS) return FALSE;

    EnterCriticalSection(&s_logger.cs);

    s_logger.callbacks[s_logger.callback_count].callback = callback;
    s_logger.callbacks[s_logger.callback_count].user_data = user_data;
    s_logger.callback_count++;

    LeaveCriticalSection(&s_logger.cs);
    return TRUE;
}

void Logger_RemoveCallback(LogCallback callback)
{
    EnterCriticalSection(&s_logger.cs);

    for (int i = 0; i < s_logger.callback_count; i++) {
        if (s_logger.callbacks[i].callback == callback) {
            /* Shift remaining entries */
            for (int j = i; j < s_logger.callback_count - 1; j++) {
                s_logger.callbacks[j] = s_logger.callbacks[j + 1];
            }
            s_logger.callback_count--;
            break;
        }
    }

    LeaveCriticalSection(&s_logger.cs);
}

void Logger_SetFileEnabled(BOOL enabled)
{
    s_logger.file_enabled = enabled;
}

const WCHAR* Logger_LevelName(LogLevel level)
{
    switch (level) {
    case LOG_DEBUG: return L"DEBUG";
    case LOG_INFO:  return L"INFO ";
    case LOG_WARN:  return L"WARN ";
    case LOG_ERROR: return L"ERROR";
    default:        return L"?????";
    }
}
