#pragma once
#include "MovementActions.h"
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
Unit* GetFankrissStackBoss(PlayerbotAI* ai);
bool FankrissHealingActive(PlayerbotAI* ai);
bool FankrissHealReachable(PlayerbotAI* ai, Unit* target);
void UpdateFankrissHealing(PlayerbotAI* ai);
class FankrissActiveBossValue : public UnitCalculatedValue
{
public:
    FankrissActiveBossValue(PlayerbotAI* ai) : UnitCalculatedValue(ai, "fankriss active boss", 1) {}
    Unit* Calculate() override;
};
class FankrissStackAction : public MovementAction
{
public:
    FankrissStackAction(PlayerbotAI* ai) : MovementAction(ai, "fankriss rear stack") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
private:
    float lastX = 0, lastY = 0;
    uint32 lastMove = 0;
};
class FankrissStackTrigger : public Trigger
{
public:
    FankrissStackTrigger(PlayerbotAI* ai) : Trigger(ai, "fankriss rear needed", 1) {}
    bool IsActive() override;
};
class FankrissStackStrategy : public Strategy
{
public:
    FankrissStackStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "fankriss stack"; }
private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
class FankrissHealingStrategy : public Strategy
{
public:
    FankrissHealingStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "fankriss healing"; }
private:
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
