#pragma once

#include <algorithm>

// Same-directory include: resolves for the module (ai/ on the path) and
// for tools/test_warrior_ww_stance_policy.cpp (compiled from the root).
#include "WarriorRageGatePolicy.h"

// Stance-dance gates for Whirlwind and Pummel. The engine runs the stance
// prerequisite before isPossible, so a plain "berserker stance" prerequisite
// swaps even when the main spell is on cooldown or unaffordable: the swap
// truncates rage and the cast fails. The dedicated prerequisite actions only
// dance when the main spell can fire this tick.
//
// The rage compared is the rage the warrior will hold AFTER entering
// Berserker Stance: already there the bar is untouched, otherwise only the
// Tactical Mastery retention survives (RageAfterStanceSwap). Without the
// talent a swap empties the bar, so even 100 rage cannot pay for Whirlwind.

namespace ai
{
    // Whirlwind costs 25 rage.
    constexpr uint32_t WARRIOR_WHIRLWIND_RAGE_COST = 25;
    // Pummel costs 10 rage.
    constexpr uint32_t WARRIOR_PUMMEL_RAGE_COST = 10;

    // True when the stance swap is worth it: the spell is off cooldown and
    // the post-swap bar holds its rage cost. In a raid group a single-target
    // Whirlwind (25 rage) is no better than Mortal Strike from Battle Stance
    // and the round trip costs two casts, so the swap needs two or more mobs
    // in melee; already in Berserker Stance no swap is paid and the cast
    // stays allowed.
    inline bool ShouldWhirlwindStanceDance(bool spellReady, bool inBerserkerStance,
                                           uint32_t rage, unsigned tmRank,
                                           bool inRaidGroup, bool twoOrMoreInMelee)
    {
        if (inRaidGroup && !twoOrMoreInMelee && !inBerserkerStance)
            return false;
        return spellReady &&
            RageAfterStanceSwap(inBerserkerStance, rage, tmRank) >= WARRIOR_WHIRLWIND_RAGE_COST;
    }

    // True when the stance swap is worth it: the spell is off cooldown, the
    // post-swap bar holds its rage cost, and the interrupt target is casting.
    inline bool ShouldPummelStanceDance(bool spellReady, bool inBerserkerStance,
                                        uint32_t rage, unsigned tmRank, bool targetCasting)
    {
        return spellReady && targetCasting &&
            RageAfterStanceSwap(inBerserkerStance, rage, tmRank) >= WARRIOR_PUMMEL_RAGE_COST;
    }
} // namespace ai
