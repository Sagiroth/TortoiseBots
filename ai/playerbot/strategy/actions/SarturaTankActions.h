#pragma once
#include "AttackAction.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
Unit* GetAssignedSarturaTankTarget(PlayerbotAI* ai);
class SarturaTankRecoverAction : public AttackAction
{
public:
    SarturaTankRecoverAction(PlayerbotAI* ai) : AttackAction(ai, "sartura tank recover") {}
    Unit* GetTarget() override { return GetAssignedSarturaTankTarget(ai); }
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class SarturaTankRecoverTrigger : public Trigger
{
public:
    SarturaTankRecoverTrigger(PlayerbotAI* ai) : Trigger(ai, "sartura tank recover needed", 1) {}
    bool IsActive() override { return SarturaTankRecoverAction(ai).isUseful(); }
};
class SarturaTankMultiplier : public Multiplier
{
public:
    SarturaTankMultiplier(PlayerbotAI* ai) : Multiplier(ai, "sartura tank assignment") {}
    float GetValue(Action* action) override;
};
class SarturaMainTankStrategy : public Strategy
{
public:
    SarturaMainTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "sartura main tank"; }
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
