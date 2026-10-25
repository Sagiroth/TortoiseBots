#pragma once
#include "AttackAction.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
Unit* GetAssignedHuhuranTankTarget(PlayerbotAI* ai);
class HuhuranTankEngageAction : public AttackAction
{
public:
    HuhuranTankEngageAction(PlayerbotAI* ai) : AttackAction(ai, "huhuran tank engage") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class HuhuranRangedAction : public MovementAction
{
public:
    HuhuranRangedAction(PlayerbotAI* ai) : MovementAction(ai, "huhuran ranged move") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class HuhuranTankTrigger : public Trigger
{
public:
    HuhuranTankTrigger(PlayerbotAI* ai) : Trigger(ai, "huhuran tank engage needed", 1) {}
    bool IsActive() override { return HuhuranTankEngageAction(ai).isUseful(); }
};
class HuhuranRangedTrigger : public Trigger
{
public:
    HuhuranRangedTrigger(PlayerbotAI* ai) : Trigger(ai, "huhuran ranged too close", 2) {}
    bool IsActive() override { return HuhuranRangedAction(ai).isUseful(); }
};
class HuhuranTankStrategy : public Strategy
{
public:
    HuhuranTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "huhuran tank"; }
private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
};
class HuhuranRangedStrategy : public Strategy
{
public:
    HuhuranRangedStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "huhuran ranged"; }
private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
