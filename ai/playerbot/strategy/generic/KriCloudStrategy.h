#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/actions/KriCloudActions.h"

namespace ai
{
    class KriCloudTrigger : public Trigger
    {
    public:
        explicit KriCloudTrigger(PlayerbotAI* ai) : Trigger(ai, "kri cloud nearby", 1) {}
        bool IsActive() override { return KriCloudTooClose(ai); }
    };

    class KriCloudStrategy : public Strategy
    {
    public:
        explicit KriCloudStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "kri cloud"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
