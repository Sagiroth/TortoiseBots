#pragma once
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class KurinnaxxSwapAction : public Action
    {
    public:
        KurinnaxxSwapAction(PlayerbotAI* ai) : Action(ai, "kurinnaxx swap") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
    class KurinnaxxHoldAction : public Action
    {
    public:
        KurinnaxxHoldAction(PlayerbotAI* ai) : Action(ai, "kurinnaxx hold attacks") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
    class KurinnaxxSwapTrigger : public Trigger
    {
    public:
        KurinnaxxSwapTrigger(PlayerbotAI* ai) : Trigger(ai, "kurinnaxx swap needed", 1) {}
        bool IsActive() override;
    };
    class KurinnaxxHoldTrigger : public Trigger
    {
    public:
        KurinnaxxHoldTrigger(PlayerbotAI* ai) : Trigger(ai, "kurinnaxx hold needed", 1) {}
        bool IsActive() override;
    };
    class KurinnaxxSwapStrategy : public Strategy
    {
    public:
        KurinnaxxSwapStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "kurinnaxx swap"; }
    private:
        void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
    };
}
