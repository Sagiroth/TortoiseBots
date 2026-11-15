#pragma once

// The CastCancel diagnostic row (AiPlayerbot.RaidBossDiagnostics).
//
// Healer and caster casts start and never land in Hakkar fights, and the cause is
// not proven: no data set logs a cancel. Every module-side cancel site calls
// LogCastCancel before it interrupts, so the row carries the bot, the spell, how
// far the cast had got, the site, the cast's target and how far the bot moved.
// Observation only: nothing here interrupts, moves or changes a cast.
//
// Gated by AiPlayerbot.RaidBossDiagnostics (which also needs bot_events.csv in
// AiPlayerbot.AllowedLogFiles). With the key off each call is one branch on a
// global bool and writes nothing.

#include "Object.h" // CurrentSpellTypes

class PlayerbotAI;

namespace ai
{
    // One `CastCancel` row for the cast held by `slot` (generic, channeled, autorepeat or melee), and nothing when
    // that slot holds no cast. A site passes the slot it really interrupts, never "whichever cast is live".
    // `movedYd` is the distance the cancel moved the bot (0 when the cancel is a plain interrupt).
    void LogCastCancel(PlayerbotAI* ai, const char* site, CurrentSpellTypes slot, float movedYd = 0.0f);

    // The rows for the slots a CastStop() drops: every non-melee slot the core stops (a preparing cast with a cast
    // time), one row per slot, written just before the call. Key off: no slot is read.
    void LogCastCancelStop(PlayerbotAI* ai, const char* site, float movedYd = 0.0f);

    // The rows for the slots an InterruptNonMeleeSpells(withDelayed) drops: generic and autorepeat unless the cast is
    // delayed, channeled always.
    void LogCastCancelNonMelee(PlayerbotAI* ai, const char* site, bool withDelayed, float movedYd = 0.0f);
} // namespace ai
