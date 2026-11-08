#pragma once

#include <cstddef>
#include <cstdint>

namespace ai
{
    // Pure gate for the priest Lesser Heal suppression. The bot reads live
    // state (spellbook, mana) into these two plain inputs, so the rule stays
    // testable in tools/test_priest_heal_line_policy.cpp without the server.
    //
    // Automatic selection drops Lesser Heal only while the bot both knows
    // Heal rank 2+ (level 22: 2055, 6063, 6064) and has the mana for at
    // least one of those ranks (PriestAnyHealRankAffordable). Mana only: a
    // cast in progress or a cooldown on Heal (walk 55: Durgrim cast Lesser
    // Heal 13 and 6 times with Heal known) no longer lets Lesser Heal through. Heal rank 1 (level 16)
    // stays on Lesser Heal's 3-rank downranking ladder; an unaffordable Heal keeps Lesser Heal as the
    // low-mana fallback. Explicit player orders are untouched (multipliers
    // only shape automatic selection).
    inline bool PriestSuppressLesserHeal(bool knowsHealRank2Plus, bool healAffordable)
    {
        return knowsHealRank2Plus && healAffordable;
    }

    // Whether the bot has the mana for at least one known Heal rank 2+. `costs` holds the mana cost of each such
    // rank the bot knows (the core's own price for it, mods included); `mana` is the bot's current mana. A bot that
    // knows none, or cannot pay even the cheapest, is not affordable and keeps Lesser Heal as its low-mana fallback.
    inline bool PriestAnyHealRankAffordable(const std::uint32_t* costs, std::size_t count, std::uint32_t mana)
    {
        for (std::size_t i = 0; i < count; ++i)
            if (costs[i] <= mana)
                return true;
        return false;
    }
}
