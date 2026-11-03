#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class FankrissSpawnStrategy : public Strategy
    {
    public:
        explicit FankrissSpawnStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        std::string getName() override { return "fankriss spawn focus"; }
    };
}
