#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BossTacticsPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include <cstdint>
#include <vector>

namespace ai
{
    // "hold kill blockers::<entry>": how many living units of the blocker entry a hold_kill row waits for (a fighting
    // Mad Voidwalker, a Portal of Madness) stand within kHoldKillRangeYd of the bot. The one grid search of the
    // hold_kill tactic, run at most once a second per bot and blocker entry: every reader (the dps target pickers, the
    // hold multiplier, the trigger and the action) goes through this value, never through a search of its own.
    class HoldKillBlockersValue : public CalculatedValue<uint32>, public Qualified
    {
    public:
        HoldKillBlockersValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "hold kill blockers", 1), Qualified() {}
        uint32 Calculate() override;
    };

    // "sole kill candidates::<entry>": the living units of the entry within kHoldKillRangeYd that fight and can be the
    // one_at_a_time pick (the whole fight's units of that entry, lowest health first, lowest guid on a tie: the pick
    // rule is BossTacticsPolicy's PickSoleKill). Never the bot's own "attackers" list, which "focus rti targets"
    // collapses to the raid mark, so every bot of the fight reads the same set. The same grid search as the blocker
    // count, at most once a second per bot and entry.
    class SoleKillCandidatesValue : public CalculatedValue<std::vector<SoleCandidate>>, public Qualified
    {
    public:
        SoleKillCandidatesValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<SoleCandidate>>(ai, "sole kill candidates", 1), Qualified() {}
        std::vector<SoleCandidate> Calculate() override;
    };
}
