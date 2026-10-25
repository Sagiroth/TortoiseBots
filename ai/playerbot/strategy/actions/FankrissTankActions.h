#pragma once
#include "AttackAction.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
Unit* GetAssignedFankrissTankTarget(PlayerbotAI* ai);
class FankrissSwapAction : public Action
{
public:
    FankrissSwapAction(PlayerbotAI* ai) : Action(ai, "fankriss swap") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class FankrissTankEngageAction : public AttackAction
{
public:
    FankrissTankEngageAction(PlayerbotAI* ai) : AttackAction(ai, "fankriss tank engage") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class FankrissSwapTrigger : public Trigger
{
public:
    FankrissSwapTrigger(PlayerbotAI* ai) : Trigger(ai, "fankriss swap needed", 1) {}
    bool IsActive() override { return FankrissSwapAction(ai).isUseful(); }
};
class FankrissTankEngageTrigger : public Trigger
{
public:
    FankrissTankEngageTrigger(PlayerbotAI* ai) : Trigger(ai, "fankriss tank engage needed", 1) {}
    bool IsActive() override { return FankrissTankEngageAction(ai).isUseful(); }
};
class FankrissTankStrategy : public Strategy
{
public:
    FankrissTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "fankriss tank"; }
private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
