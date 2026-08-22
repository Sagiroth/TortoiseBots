#pragma once
//
// BotActionLog — opt-in per-bot detailed action log. Each bot summoned while
// AiPlayerbot.EnableActionLog=1 gets its own file under logs/bots/, with a
// timestamped event stream covering lifecycle, casts, auras, state and target
// changes. One event per line, `[timestamp] [TAG] key=val key=val`. Files are
// fflush()'d on every write so a crash doesn't lose the trail. Gated behind
// the runtime config flag; ~zero cost when disabled.
//

#include "Common.h"
#include <cstdio>
#include <unordered_map>

class Player;
class PlayerbotAI;

namespace ai { namespace botdiag {

class BotActionLog
{
public:
    // Open a log file for this bot. Idempotent (returns existing handle
    // if already open). Creates `logs/bots/` if missing. Returns null on
    // I/O error (caller must tolerate null and skip logging).
    static std::FILE* Open(PlayerbotAI* ai);

    // Close + flush + remove from the registry. Safe to call on a bot
    // that was never opened.
    static void Close(PlayerbotAI* ai);

    // Get the open handle for this bot, or null if not open. Cheap.
    static std::FILE* GetHandle(PlayerbotAI* ai);

    // Lower-level write: timestamped, tag-prefixed, fflush'd. Format
    // follows printf semantics. Safe to call when log isn't open
    // (no-op).
    static void Write(PlayerbotAI* ai, const char* tag, const char* fmt, ...);

    // Convenience: snapshot the bot's current state to the log. Called
    // periodically (every ~2s from TickHeartbeat) and on combat-state
    // change. Includes HP%, MP%, combat flag, target name+guid, active
    // strategy list, current spell-cast (if any), aura count.
    static void LogState(PlayerbotAI* ai, const char* reason);

    // Convenience: log a spell-cast attempt with its target.
    static void LogCastStart(PlayerbotAI* ai, uint32 spellId, ObjectGuid targetGuid, uint32 castTimeMs);

    // Convenience: log a spell-cast result. `result` is a SpellCastResult
    // enum value (0 = success, otherwise failure code from SpellMgr).
    // `phase` is "PREPARE" / "CAST" / "EFFECT" — where in the cast
    // pipeline the result fired.
    static void LogCastResult(PlayerbotAI* ai, uint32 spellId, uint8 result, const char* phase);

    // Convenience: log an aura apply/remove. We skip very-short auras
    // (< 3s) and movement-class auras to keep the log readable; pass
    // `force=true` to always log.
    static void LogAuraApply(PlayerbotAI* ai, uint32 spellId, int32 durationMs, ObjectGuid casterGuid, bool force = false);
    static void LogAuraRemove(PlayerbotAI* ai, uint32 spellId, ObjectGuid casterGuid, bool force = false);

private:
    // Map keyed by bot character GUID (low). Player* would be unsafe
    // across logout; the GUID stays valid in the map until we Close it.
    static std::unordered_map<uint32, std::FILE*> sFiles;

    // Helper: build the per-bot log path. `<botname>_<sessionId>_<date>.log`.
    static std::string BuildPath(Player* bot);

    // Helper: ensure `logs/bots/` exists. Cheap (CreateDirectory ignores
    // ERROR_ALREADY_EXISTS).
    static void EnsureLogDir();
};

}}  // namespace ai::botdiag
