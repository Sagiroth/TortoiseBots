#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    // "hold kill blockers::<entry>": how many living units of the blocker entry a hold_kill row waits for (a fighting
    // Mad Voidwalker, a Portal of Madness) stand within kHoldKillRangeYd of the bot. The one grid search of the hold_kill
    // tactic, run at most once a second per bot and blocker entry: every reader (the dps target pickers, the hold
    // multiplier, the trigger and the action) goes through this value, never through a search of its own.
    class HoldKillBlockersValue : public CalculatedValue<uint32>, public Qualified
    {
    public:
        HoldKillBlockersValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "hold kill blockers", 1), Qualified() {}
        uint32 Calculate() override;
    };
}
