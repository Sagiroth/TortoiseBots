#pragma once

#include <cstdint>

// Pure decision rule for the mage Hot Streak proc port (MAG-1): Turtle's
// fire talent Hot Streak (51927/51928) procs a stacking cast-time
// reduction aura for the next Pyroblast (proc 51930/51931, aura 107 flat
// modifier, -1001ms per stack, 5 stacks, single charge). At full stacks
// the 6s Pyroblast casts in ~1s; at 1 stack it still takes ~5s, so firing
// early wastes the proc.
//
// Two traps this rule encodes, both found in review of the first version:
//  1. The passive talent auras 51927/51928 share the exact SpellName "Hot
//     Streak" and sit on the bot permanently, so a name-based HasAura
//     check fires on every tick for any talented bot. Only the proc IDs
//     51930/51931 count.
//  2. Only full stacks justify interrupting the rotation. The engine
//     spends the trigger's winning action immediately, consuming the
//     single charge — a 1-stack proc must keep stacking.
//
// No core includes: callers in strategy/ translate game state into these
// plain inputs, so the rule stays testable in
// tools/test_hot_streak_policy.cpp without the server.
//
// Donor: mod-playerbots `FireMageStrategy.cpp:51-58` (`hot streak` ->
// `pyroblast`) with `HotStreakTrigger : HasAuraTrigger("hot streak")` —
// adapted: donor's WotLK Hot Streak is a binary instant-cast proc, so a
// name check suffices there; Turtle's stacking variant needs the proc-ID
// + full-stack gate below.

namespace ai
{
    // Hot Streak proc aura ids (the stacking cast-time reduction, not the
    // passive talent auras 51927/51928 which share the name).
    constexpr std::uint32_t HOT_STREAK_PROC_RANK_1 = 51930;
    constexpr std::uint32_t HOT_STREAK_PROC_RANK_2 = 51931;

    // Full-stack count of the proc aura (StackAmount 5 in spell_template).
    constexpr std::uint32_t HOT_STREAK_FULL_STACKS = 5;

    struct HotStreakState
    {
        bool hasProcRank1;       // aura 51930 present on the bot
        std::uint32_t procRank1Stacks;  // its stack count
        bool hasProcRank2;       // aura 51931 present on the bot
        std::uint32_t procRank2Stacks;  // its stack count
    };

    inline bool IsHotStreakProcId(std::uint32_t spellId)
    {
        return spellId == HOT_STREAK_PROC_RANK_1 ||
               spellId == HOT_STREAK_PROC_RANK_2;
    }

    // True when a Hot Streak proc aura sits at full stacks: hurry a
    // Pyroblast now. Talent auras, partial stacks, and no proc all stay
    // quiet so the normal rotation (and the stacking itself) continues.
    inline bool ShouldCastHotStreakPyroblast(HotStreakState const& state)
    {
        if (state.hasProcRank1 && state.procRank1Stacks >= HOT_STREAK_FULL_STACKS)
            return true;
        if (state.hasProcRank2 && state.procRank2Stacks >= HOT_STREAK_FULL_STACKS)
            return true;
        return false;
    }
}
