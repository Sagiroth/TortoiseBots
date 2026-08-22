#pragma once
//
// BotLog — routes all sLog calls from the bot subsystem to a dedicated log
// file (logs/bots.log by default) so the main server.log stays clean.
//
// Activated by AiPlayerbot.BotLogFile in aiplayerbot.conf. When the setting
// is empty the class falls back to Log::Instance() for every call, keeping
// existing behaviour with zero overhead.
//
// playerbot.h redefines `sLog` to `BotLog::Instance()` after including this
// header, so every bot .cpp that includes playerbot.h automatically routes
// through here without any call-site changes.
//

#include "Log.h"
#include "Platform/Define.h"
#include <cstdio>
#include <mutex>

class BotLog
{
public:
    static BotLog& Instance();

    // Called once from PlayerbotAIConfig::Initialize(). logFile is the bare
    // filename (e.g. "bots.log"); logsDir is the server's LogsDir value.
    // Empty logFile disables file routing (all calls fall through to sLog).
    void Initialize(const char* logFile, const char* logsDir, bool debugEnabled = false);

    // Drop-in replacements for the Log methods bot code calls directly.
    void outString();
    void outString(const char* fmt, ...)  ATTR_PRINTF(2, 3);
    void outInfo(const char* fmt, ...)    ATTR_PRINTF(2, 3);
    void outDetail(const char* fmt, ...)  ATTR_PRINTF(2, 3);
    void outError(const char* fmt, ...)   ATTR_PRINTF(2, 3);
    void outDebug(const char* fmt, ...)   ATTR_PRINTF(2, 3);
    void outBasic(const char* fmt, ...)   ATTR_PRINTF(2, 3);
    void outErrorDb(const char* fmt, ...) ATTR_PRINTF(2, 3);

    // The LOG macros in Log.h (DETAIL_LOG, BASIC_LOG, etc.) call these on
    // sLog before calling out*. Return permissive values so the macros
    // always forward to our out* methods.
    bool HasLogLevelOrHigher(LogLevel /*loglvl*/) const { return true; }
    bool HasLogFilter(uint32 /*filter*/) const { return false; }

private:
    FILE*      m_file = nullptr;
    bool       m_debugEnabled = false;
    std::mutex m_mutex;
};
