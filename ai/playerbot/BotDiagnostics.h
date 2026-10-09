#pragma once

// Opt-in observability for the playerbots subsystem.
//
//   SC_LOG(fmt, ...)         -> sLog.outDetail("[BOT] ..."). Grep "\[BOT\]"
//                               in info.log to see all bot debug output.
//   SC_PHASE(tag, botName)   -> stamps a TLS marker that mangosd's unhandled-
//                               exception filter reads to identify which
//                               subsystem the crashing thread was in.
//
// Both gated at runtime on AiPlayerbot.EnableActionLog (default off). When
// off, each call is one branch on a global bool and the compiler typically
// folds it away.
//
// The SC_ prefix is a holdover from the project's internal name for the bot
// diagnostic harness ("SoloCommander"); kept as-is to avoid a churn rename
// across ~80 call sites. Read SC_ as "bot diag".

#include "Log.h"
#include <cstdint>

namespace ai { namespace botdiag {
    bool IsActionLogEnabled();

    // Aggregate counter of executed bot actions (AiPlayerbot.ActionCountsLog,
    // default off). Engine records the OK / FAILED / IMPOSSIBLE outcome of each
    // executed action keyed by (bot class, action name); BotManager dumps a
    // cumulative CSV snapshot every 5 minutes. Bots update on parallel map
    // threads, so counting takes a mutex; the off path is one branch.
    // Player is deliberately not named: this header must stay free of the AI
    // class (see the evade probe note below), so call sites pass the already
    // resolved class id and the action name.
    void CountAction(uint8_t botClass, char const* actionName, bool ok);
    void DumpActionCounts();

    // State of the evade probe in PlayerbotAI::UpdateEvadeProbe (implemented in
    // BotDiagnostics.cpp). Kept here as plain data: this header is included by host code
    // that also carries a global `class PlayerbotAI;` forward declaration, so it must not
    // name the AI class at all (doing so made the name ambiguous in those translation
    // units).
    //
    // The probe is observation only - it never sets an action, a value or movement. While
    // a bot holds a creature target (its current target, or the attack target it was
    // ordered to grind), a sample every kSampleSec records the mob's health and evade
    // state, and a row is written to bot_events.csv ("EvadeProbe") when the mob is in
    // evade mode, is evading because its target is unreachable, or gained 15+ health
    // points since the previous sample while the bot was fighting it - regeneration
    // outrunning the bot's damage. At most one row per bot per 30 s.
    //
    // Alongside the mob's health and distance, the row carries what the creature is doing
    // and why it is untouchable: its current movement generator (6/HOME means it was sent
    // home by EnterEvadeMode and is walking back), how far it is from the spawn point it
    // is walking to and from the position its current fight started at (the core stops
    // counting a victim that left the threat radius around that point), how many units
    // still attack it, the threat the bot itself holds, and whether the bot's pet is the
    // one fighting it. Together those separate "the fight dragged it away from home",
    // "the last attacker died or left", and "a pet pulled it and its owner walked off".
    //
    // A counter row ("EvadeProbeCounter") per fighting bot per minute carries the fights
    // sampled, how many of those had a mob that gained health, and how many samples saw an
    // evading or an unreachable mob, so rates can be computed from the log alone.
    struct EvadeProbeState
    {
        uint32 lastSampleSec = 0;
        uint32 lastLogSec = 0;
        uint32 lastCounterSec = 0;
        uint64 sampledTarget = 0;      // guid of the mob the previous sample was taken from
        uint32 sampledSec = 0;         // when that sample was taken (for the health rate)
        uint32 sampledHealthPct = 0;
        uint64 riseCountedTarget = 0;  // fight already counted as "mob gained health"
        uint32 counterFights = 0;
        uint32 counterRiseFights = 0;
        uint32 counterEvade = 0;
        uint32 counterNoReach = 0;
        uint32 counterRows = 0;
    };

    extern thread_local const char* gLastPhaseTag;
    extern thread_local const char* gLastPhaseBotName;
}}

#define SC_LOG(fmt, ...) do { \
    if (ai::botdiag::IsActionLogEnabled()) \
        sLog.outDetail("[BOT] " fmt, ##__VA_ARGS__); \
} while (0)

#define SC_PHASE(tag, botName) do { \
    if (ai::botdiag::IsActionLogEnabled()) { \
        ai::botdiag::gLastPhaseTag     = (tag); \
        ai::botdiag::gLastPhaseBotName = (botName); \
    } \
} while (0)
