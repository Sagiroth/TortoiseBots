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
// Spare tanks only. The existing wound-swap role always wins if both are set.
Unit* GetAssignedFankrissSpawnTankTarget(PlayerbotAI* ai);
class FankrissSpawnEngageAction : public AttackAction
{
public:
    FankrissSpawnEngageAction(PlayerbotAI* ai) : AttackAction(ai, "fankriss spawn engage") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class FankrissSpawnPickupAction : public Action
{
public:
    FankrissSpawnPickupAction(PlayerbotAI* ai) : Action(ai, "fankriss spawn pickup") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class FankrissSpawnEngageTrigger : public Trigger
{
public:
    FankrissSpawnEngageTrigger(PlayerbotAI* ai) : Trigger(ai, "fankriss spawn engage needed", 1) {}
    bool IsActive() override { return FankrissSpawnEngageAction(ai).isUseful(); }
};
class FankrissSpawnPickupTrigger : public Trigger
{
public:
    FankrissSpawnPickupTrigger(PlayerbotAI* ai) : Trigger(ai, "fankriss spawn pickup needed", 1) {}
    bool IsActive() override { return FankrissSpawnPickupAction(ai).isUseful(); }
};
class FankrissSpawnTankStrategy : public Strategy
{
public:
    FankrissSpawnTankStrategy(PlayerbotAI* ai) : Strategy(ai) {}
    std::string getName() override { return "fankriss spawn tank"; }
private:
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
};

}
