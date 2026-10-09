#pragma once

#include <string>
#include <cstdint>

// Pure decision rules for the Baron Geddon Inferno runout
// (mod-playerbots parity, raid1 batch item 1).
// Donor: mod-playerbots @ 79bd4281, src/Ai/Raid/MC/MCTriggers.cpp
// (McBaronGeddonInfernoTrigger: boss has SPELL_INFERNO) +
// src/Ai/Raid/MC/MCActions.cpp:34-49 (McMoveFromBaronGeddonAction: everyone
// runs 20y out, stops casts first) + src/Ai/Raid/MC/MCMultipliers.cpp
// (BaronGeddonAbilityMultiplier: only the runout moves while Inferno or
// Living Bomb is up). No core includes: the trigger/action translate game
// state into these plain inputs, so the rules stay testable in
// tools/test_geddon_inferno_policy.cpp.

namespace ai
{
    // Baron Geddon entry + Inferno/Living Bomb spell ids (1.18.1 verified).
    constexpr std::uint32_t kGeddonEntry = 12056;
    constexpr std::uint32_t kInfernoSpellId = 19695;
    constexpr std::uint32_t kLivingBombSpellId = 20475;

    // Donor runout distance: INFERNO_DISTANCE = LIVING_BOMB_DISTANCE = 20y.
    constexpr float kGeddonRunoutDistance = 20.0f;

    // Trigger: boss Geddon is an active attacker and carries the Inferno
    // aura. Cheap checks (entry match, aura presence) first; no world scan.
    inline bool ShouldRunFromGeddonInferno(bool bossPresent, bool bossHasInfernoAura)
    {
        return bossPresent && bossHasInfernoAura;
    }

    // Multiplier: while Inferno is up on the boss (or the bot carries Living
    // Bomb, which outlives the boss), only the two survival moves may move
    // the bot: the inferno runout and the universal bomb runout. Mirrors
    // the donor's BaronGeddonAbilityMultiplier, which vetoes MovementAction
    // (except the two runouts) and CastReachTargetSpellAction — heals, DPS,
    // threat and consumables always pass. The caller computes
    // actionMovesOrReaches via dynamic_cast; the policy stays free of Action
    // types so it remains unit-testable.
    inline bool IsGeddonSurvivalMove(const std::string& actionName)
    {
        return actionName == "move away from geddon" ||
               actionName == "raid bomb runout";
    }

    inline bool ShouldBlockGeddonMove(bool actionMovesOrReaches, const std::string& actionName,
        bool infernoActive, bool bombOnSelf)
    {
        if (!actionMovesOrReaches)
            return false;
        if (!infernoActive && !bombOnSelf)
            return false;
        return !IsGeddonSurvivalMove(actionName);
    }
}
