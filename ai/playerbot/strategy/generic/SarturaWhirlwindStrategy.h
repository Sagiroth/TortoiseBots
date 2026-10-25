#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/actions/SarturaWhirlwindActions.h"

namespace ai
{
    class SarturaWhirlwindTrigger : public Trigger
    {
    public:
        explicit SarturaWhirlwindTrigger(PlayerbotAI* ai) : Trigger(ai, "sartura whirlwind nearby", 1) {}
        bool IsActive() override { return SarturaWhirlwindTooClose(ai); }
    };

    class SarturaWhirlwindStrategy : public Strategy
    {
    public:
        explicit SarturaWhirlwindStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "sartura whirlwind"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
