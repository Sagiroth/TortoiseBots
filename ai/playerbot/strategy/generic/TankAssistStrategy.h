#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class TankAssistStrategy : public Strategy
    {
    public:
        TankAssistStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "tank assist"; }
        int GetType() override { return STRATEGY_TYPE_TANK; }

    private:
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitCombatTriggers(std::list<TriggerNode*> &triggers) override;
    };
}
