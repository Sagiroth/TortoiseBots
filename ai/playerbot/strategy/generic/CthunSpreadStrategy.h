#pragma once
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/actions/CthunSpreadActions.h"
namespace ai
{
    class CthunSpreadTrigger : public Trigger
    {
    public:
        explicit CthunSpreadTrigger(PlayerbotAI* ai) : Trigger(ai, "cthun spread needed", 1) {}
        bool IsActive() override
        { return ai->GetAiObjectContext()->GetAction("cthun spread move")->isUseful(); }
    };
    class CthunSpreadTargetTrigger : public Trigger
    {
    public:
        explicit CthunSpreadTargetTrigger(PlayerbotAI* ai) : Trigger(ai,"cthun sector target",1) {}
        bool IsActive() override;
    };
    class CthunSpreadStrategy : public Strategy
    {
    public:
        explicit CthunSpreadStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "cthun spread"; }
    private:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
