#pragma once
//
// BotActionLog — opt-in per-bot detailed action log. Each bot summoned while
// AiPlayerbot.EnableActionLog=1 gets its own file under logs/bots/, with a
// timestamped event stream covering lifecycle, casts, auras, state and target
// changes. One event per line, `[timestamp] [TAG] key=val key=val`. Files are
// fflush()'d on every write so a crash doesn't lose the trail. Gated behind
// the runtime config flag; ~zero cost when disabled.
//
// Disk safeguards (ActionLogPolicy.h): AiPlayerbot.ActionLogScope picks which
// bots write a file (default: only commander-bound or hired ones, the
// answer cached per bot and looked up again every 15 s on the world thread);
// AiPlayerbot.ActionLogMaxFileMB rotates a file that grows past the cap;
// AiPlayerbot.ActionLogMaxAgeDays / ActionLogMaxTotalMB delete the oldest
// files at startup and hourly, never one a bot has open.
//

#include "Common.h"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

    // Close every open file (config turned off, shutdown). World thread.
    static void CloseAll();

    // Guid-keyed close for logout/shutdown, where the Player object is
    // already gone but the low guid was recorded at login.
    static void Close(uint32 botGuidLow);
    // Next login for this guid: allow Open again after the logout tombstone.
    // World thread, before the AI is published to storage.
    static void NoteLogin(uint32 botGuidLow);

    // Get the open handle for this bot, or null if not open. Cheap.
    static std::FILE* GetHandle(PlayerbotAI* ai);

    // World thread, once per bot AI update: re-reads whether the bot is in
    // scope (ActionLogScope = commanded) at most every 15 s and closes its
    // file when it just left the scope. Costs one branch when the log is off or
    // the scope is all/none.
    static void RefreshScope(PlayerbotAI* ai);

    // Hook for a bot-trace feature (nothing on dev calls it yet): marks one bot
    // as traced, so scope commanded logs it, and clears it again when the trace
    // ends. World thread.
    static void SetTraced(uint32 botGuidLow, bool traced);

    // World thread, once per world update: runs the retention cleanup at
    // startup, then hourly (or sooner after heavy writing). One summary line per
    // cleanup. Works even while EnableActionLog is off, so files left by an
    // earlier run are still trimmed.
    static void Maintain();

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
    struct OpenFile
    {
        std::FILE* file = nullptr;
        std::string path;
        uint64 bytes = 0;  // size so far, for the per-file cap
    };
    // Map keyed by bot character GUID (low). Player* would be unsafe
    // across logout; the GUID stays valid in the map until we Close it.
    static std::unordered_map<uint32, OpenFile> sFiles;

    // Cached scope answer per bot (ActionLogScope = commanded).
    struct ScopeState
    {
        bool allowed = false;
        int64 nextCheckMs = 0;
    };
    static std::unordered_map<uint32, ScopeState> sScope;
    static std::unordered_set<uint32> sTraced;
    // Guids whose AI is gone. A map-thread hook may still hold the dead AI
    // pointer; refusing its reopen keeps logout from leaking a fresh fd.
    static std::unordered_set<uint32> sRetired;

    static bool ScopeAllowedLocked(uint32 guid);
    static void AppendLine(PlayerbotAI* ai, const char* line, size_t len);
    static void Sweep();

    // Helper: build the per-bot log path. `<botname>_<sessionId>_<date>.log`.
    static std::string BuildPath(Player* bot, uint32 seq = 0);

    // Helper: ensure `logs/bots/` exists. Cheap (CreateDirectory ignores
    // ERROR_ALREADY_EXISTS).
    static void EnsureLogDir();
};

}}  // namespace ai::botdiag
