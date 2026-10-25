#pragma once
#include "DungeonActions.h"

namespace ai
{
    Unit* GetActiveThaddius(PlayerbotAI* ai);
    bool HasThaddiusPolarity(Player* player);
    bool ShouldHoldThaddiusPolarityPosition(PlayerbotAI* ai);

    class ThaddiusPolarityStackAction : public MovementAction
    {
    public:
        ThaddiusPolarityStackAction(PlayerbotAI* ai) : MovementAction(ai, "thaddius polarity stack") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
}
