#pragma once

// Pure policy for the tank's off-target taunt: which engaged hostile to
// Taunt when it is hitting a non-tank group member, the healer's attacker
// first. Kept std-only so the standalone g++ policy test (tools/test_*.cpp)
// compiles the real rules; the caller (AttackerOfNonTankTargetValue) fills
// in the game state: unit lookup, victim roles, CcGuard hold tests and
// distance.

#include <cstddef>
#include <vector>

namespace ai
{
    struct AttackerOfNonTankCandidate
    {
        // Players have no threat list: a taunt on one does nothing. The
        // caller only feeds creatures.
        bool victimIsGroupPlayer = false; // victim is the bot, its master or a group member
        bool victimIsTank = false;        // victim holds it already: no taunt needed
        bool victimIsHealer = false;      // healer victims outrank the rest
        bool heldCcMark = false;          // a group CC mark the party leaves alone (CcGuard)
        bool heldMarkTauntAllowed = false; // CcHeldMarkTauntAllowed: tank Taunt only, not
                                           // crowd-controlled yet, no CC cast bar, healer victim
        bool tauntRefused = false;        // TauntGateFor refused it: a needs_aura subject (the Shade
                                          // of Jin'do) or a taunt-immune creature
        float distance = 0.0f;            // 2D distance to the tank, the closeness key
    };

    // A candidate is tauntable when a group member needs it off them; the
    // cast's own spell range (read from the spell, not a constant) and its
    // reach prerequisite move the tank into range. The owner exception only
    // ever applies to a healer victim (the caller can only set
    // heldMarkTauntAllowed then); the policy re-checks it so the rule stands
    // on its own.
    inline bool AttackerOfNonTankUsable(AttackerOfNonTankCandidate const& candidate)
    {
        return candidate.victimIsGroupPlayer && !candidate.victimIsTank && !candidate.tauntRefused &&
            (!candidate.heldCcMark || (candidate.heldMarkTauntAllowed && candidate.victimIsHealer));
    }

    // Returns the chosen index, or candidates.size() when nothing qualifies.
    // Priority: a mob on the healer first (the healer's death kills the
    // party, a hit on a DPS does not), then any other non-tank member;
    // closest mob first within the same rank so a pack split on two members
    // takes the nearer one.
    inline std::size_t AttackerOfNonTankPick(std::vector<AttackerOfNonTankCandidate> const& candidates)
    {
        std::size_t best = candidates.size();
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            if (!AttackerOfNonTankUsable(candidates[i]))
                continue;

            if (best == candidates.size())
            {
                best = i;
                continue;
            }

            if (candidates[i].victimIsHealer != candidates[best].victimIsHealer)
            {
                if (candidates[i].victimIsHealer)
                    best = i;
                continue;
            }

            if (candidates[i].distance < candidates[best].distance)
                best = i;
        }

        return best;
    }
}
