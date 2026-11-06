#pragma once
#include "playerbot/PlayerbotAI.h"
#include "MovementActions.h"

namespace ai
{
    // Enchant-order lifecycle: the Meet walk and the two Meet trades. Runs on
    // the guild strategy's "very often" tick while the bot holds an unfinished
    // enchant row. Gathering itself stays on the supply ladder (W10); this
    // action only moves a Ready/Waiting row through Coming/Enchanting once a
    // Meet press is pending, then back on expiry or forward to Done.
    class GuildEnchantOrderAction : public MovementAction
    {
    public:
        GuildEnchantOrderAction(PlayerbotAI* ai) : MovementAction(ai, "guild enchant order") {}

        bool Execute(Event& event) override;
        bool isUseful() override;
    };
}
