#pragma once

#include <cstdint>

namespace ai
{
    // Pure gate for the priest Lesser Heal suppression. The bot reads live
    // state (spellbook, mana) into these plain inputs, so the rule stays
    // testable in tools/test_priest_heal_line_policy.cpp without the server.
    //
    // Automatic selection drops Lesser Heal only while the bot both knows
    // Heal rank 2+ (level 22: 2055, 6063, 6064) and has the mana for the
    // Heal rank the "heal" action will probe (PriestProbedHealAffordable):
    // "heal on party" clears the rank override, so it probes the TOP known
    // rank and refuses it on that rank's price; the downrank to a cheaper
    // rank only happens in Execute, after the probe passed. Pricing a
    // cheaper rank would veto Lesser Heal and leave the priest with no heal
    // at all in the mana band between the two. Mana only: a cast in
    // progress or a cooldown on Heal (walk 55: Durgrim cast Lesser Heal 13
    // and 6 times with Heal known) no longer lets Lesser Heal through. Heal
    // rank 1 (level 16) stays on Lesser Heal's 3-rank downranking ladder; an
    // unaffordable Heal keeps Lesser Heal as the low-mana fallback. Explicit
    // player orders are untouched (multipliers only shape automatic
    // selection).
    inline bool PriestSuppressLesserHeal(bool knowsHealRank2Plus, bool healAffordable)
    {
        return knowsHealRank2Plus && healAffordable;
    }

    // Whether the bot can pay the Heal rank the "heal" action probes. `probedCost` is the mana cost of the spell the
    // "spell id" value resolves "heal" to (the top known rank), as Spell::CalculatePowerCost prices it with no spell
    // object: the flat school modifier, the percent-of-base-mana part and the aura cost multiplier are counted, but
    // SPELLMOD_COST (Improved Healing, Inner Focus) is not, the same as the cast probe's own check. The price is
    // therefore a little high, which leans toward letting Lesser Heal through. `mana` is the bot's current mana. The
    // core refuses a cast only when mana < cost.
    inline bool PriestProbedHealAffordable(std::uint32_t probedCost, std::uint32_t mana)
    {
        return probedCost <= mana;
    }

    // The spell the wand probe tries for the slow heal: "heal" while Lesser Heal is suppressed (the probe then tells the
    // wand to stop for a castable Heal), "lesser heal" otherwise. It reads the same suppression the multiplier does, so
    // in the mana band between rank 2 and the top rank the probe asks for Lesser Heal, the heal the bot will cast.
    inline const char* PriestSlowHealProbeSpell(bool lesserHealSuppressed)
    {
        return lesserHealSuppressed ? "heal" : "lesser heal";
    }
}
