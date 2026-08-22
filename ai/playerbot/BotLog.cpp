#include "BotLog.h"

#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string>

BotLog& BotLog::Instance()
{
    static BotLog s_instance;
    return s_instance;
}

void BotLog::Initialize(const char* logFile, const char* logsDir, bool debugEnabled)
{
    if (!logFile || logFile[0] == '\0')
        return;  // empty → fall through to Log::Instance() on every call

    std::string path;
    if (logsDir && logsDir[0] != '\0')
    {
        path = logsDir;
        if (path.back() != '/' && path.back() != '\\')
            path += '/';
    }
    path += logFile;

    std::lock_guard<std::mutex> g(m_mutex);
    if (m_file)
    {
        fclose(m_file);
        m_file = nullptr;
    }
    m_file = fopen(path.c_str(), "a");
    if (!m_file)
    {
        Log::Instance().outError("[BotLog] Failed to open bot log file: %s", path.c_str());
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &lt);
    m_debugEnabled = debugEnabled;
    fprintf(m_file, "\n# ---- BotLog session started %s ----\n", tsBuf);
    fflush(m_file);
}

// Format the message into a fixed buffer, then route to file or sLog.
// Using a macro to keep the call-sites DRY while still being able to do
// va_start / va_end in the caller function (va_list can't cross helpers cleanly).
#define BOTLOG_IMPL(prefix, slog_fn)                    \
    char _msg[4096];                                    \
    va_list _ap;                                        \
    va_start(_ap, fmt);                                 \
    vsnprintf(_msg, sizeof(_msg), fmt, _ap);            \
    va_end(_ap);                                        \
    std::lock_guard<std::mutex> _g(m_mutex);            \
    if (m_file) {                                       \
        std::time_t _now = std::time(nullptr);          \
        std::tm _lt{};                                  \
        localtime_r(&_now, &_lt);                       \
        char _ts[16];                                   \
        std::strftime(_ts, sizeof(_ts), "%H:%M:%S", &_lt); \
        fprintf(m_file, "[%s] %s%s\n", _ts, prefix, _msg); \
        fflush(m_file);                                 \
    } else {                                            \
        Log::Instance().slog_fn("%s", _msg);            \
    }

#ifdef _WIN32
// localtime_r is POSIX; use localtime_s on Windows
#undef BOTLOG_IMPL
#define BOTLOG_IMPL(prefix, slog_fn)                    \
    char _msg[4096];                                    \
    va_list _ap;                                        \
    va_start(_ap, fmt);                                 \
    vsnprintf(_msg, sizeof(_msg), fmt, _ap);            \
    va_end(_ap);                                        \
    std::lock_guard<std::mutex> _g(m_mutex);            \
    if (m_file) {                                       \
        std::time_t _now = std::time(nullptr);          \
        std::tm _lt{};                                  \
        localtime_s(&_lt, &_now);                       \
        char _ts[16];                                   \
        std::strftime(_ts, sizeof(_ts), "%H:%M:%S", &_lt); \
        fprintf(m_file, "[%s] %s%s\n", _ts, prefix, _msg); \
        fflush(m_file);                                 \
    } else {                                            \
        Log::Instance().slog_fn("%s", _msg);            \
    }
#endif

void BotLog::outString()
{
    std::lock_guard<std::mutex> g(m_mutex);
    if (m_file)
    {
        fprintf(m_file, "\n");
        fflush(m_file);
    }
    else
        Log::Instance().outString();
}

void BotLog::outString(const char* fmt, ...)
{
    BOTLOG_IMPL("", outString)
}

void BotLog::outInfo(const char* fmt, ...)
{
    BOTLOG_IMPL("[INFO] ", outInfo)
}

void BotLog::outDetail(const char* fmt, ...)
{
    BOTLOG_IMPL("[DETAIL] ", outDetail)
}

void BotLog::outError(const char* fmt, ...)
{
    BOTLOG_IMPL("[ERROR] ", outError)
}

void BotLog::outDebug(const char* fmt, ...)
{
    if (!m_debugEnabled)
    {
        // Debug suppressed by default; fall through to sLog when no file is open.
        std::lock_guard<std::mutex> g(m_mutex);
        if (!m_file)
        {
            char _msg[4096];
            va_list _ap;
            va_start(_ap, fmt);
            vsnprintf(_msg, sizeof(_msg), fmt, _ap);
            va_end(_ap);
            Log::Instance().outDebug("%s", _msg);
        }
        return;
    }
    BOTLOG_IMPL("[DEBUG] ", outDebug)
}

void BotLog::outBasic(const char* fmt, ...)
{
    BOTLOG_IMPL("[BASIC] ", outBasic)
}

void BotLog::outErrorDb(const char* fmt, ...)
{
    BOTLOG_IMPL("[ERROR_DB] ", outErrorDb)
}
