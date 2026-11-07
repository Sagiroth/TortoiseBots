#pragma once

#include <cstdint>

namespace ai
{
    // Pure gate for the priest Lesser Heal suppression. The bot reads live
    // state (spellbook, mana) into these two plain inputs, so the rule stays
    // testable in tools/test_priest_heal_line_policy.cpp without the server.
    //
    // Automatic selection drops Lesser Heal only while the bot both knows
    // Heal rank 2+ (level 22: 2055, 6063, 6064) and could pay for the Heal
    // cast now. Heal rank 1 (level 16) stays on Lesser Heal's 3-rank
    // downranking ladder; an unaffordable Heal keeps Lesser Heal as the
    // low-mana fallback. Explicit player orders are untouched (multipliers
    // only shape automatic selection).
    inline bool PriestSuppressLesserHeal(bool knowsHealRank2Plus, bool healAffordable)
    {
        return knowsHealRank2Plus && healAffordable;
    }
}
