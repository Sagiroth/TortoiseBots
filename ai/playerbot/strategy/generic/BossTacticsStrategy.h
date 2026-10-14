#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // Data-driven boss tactics (rows of ai_boss_tactic, see BossTacticsPolicy.h).
    // Added by the "enter boss tactics" trigger of the dungeon strategy only when
    // the gate allows it, so a default install never runs any of it.
    class BossTacticsStrategy : public Strategy
    {
    public:
        BossTacticsStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "boss tactics"; }

    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
