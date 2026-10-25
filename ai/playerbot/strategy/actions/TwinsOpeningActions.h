#pragma once
#include "AttackAction.h"
#include "playerbot/TwinsOpeningPolicy.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Strategy.h"

namespace ai
{
bool TwinsOpeningEnabled(PlayerbotAI* ai);
bool TwinsOpeningTank(PlayerbotAI* ai);
bool TwinsOpeningRouteAllowed(PlayerbotAI* ai,std::vector<WorldPosition> const& path);
bool TwinsOpeningChaseAllowed(PlayerbotAI* ai,WorldObject* target,float range);
bool SelectTwinsTankTarget(PlayerbotAI* ai,Unit*& target);
bool SelectTwinsDpsTarget(PlayerbotAI* ai,Unit*& target);
bool IsTwinsOpeningTargetExcluded(PlayerbotAI* ai,Unit* target);
bool TwinsOpeningMoveAllowed(PlayerbotAI* ai,float x,float y,float z);
bool TwinsPetAttackAllowed(PlayerbotAI* ai,Unit* target);
std::list<HazardPosition> GetTwinsOpeningHazards(PlayerbotAI* ai);
class TwinsPositionAction : public MovementAction
{
public:
    TwinsPositionAction(PlayerbotAI* ai) : MovementAction(ai,"twins position") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
private:
    bool Prepare();
    uint32 lastSpline=0,lastCheck=0,lastReport=0;
    bool prepared=false;
    TwinsPoint destination,lastDestination;
    std::vector<WorldPosition> path;
};
class TwinsEngageAction : public AttackAction
{
public:
    TwinsEngageAction(PlayerbotAI* ai) : AttackAction(ai,"twins engage") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class TwinsSearingPainAction : public Action
{
public:
    TwinsSearingPainAction(PlayerbotAI* ai) : Action(ai,"twins searing pain") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
};
class TwinsOpeningTrigger : public Trigger
{
public:
    TwinsOpeningTrigger(PlayerbotAI* ai,std::string name,std::string action) : Trigger(ai,name,1),action(action) {}
    bool IsActive() override;
private:
    std::string action;
};
class TwinsSideStrategy : public Strategy
{
public:
    TwinsSideStrategy(PlayerbotAI* ai,std::string name) : Strategy(ai),name(name) {}
    std::string getName() override { return name; }
private:
    std::string name;
};
class TwinsOpeningStrategy : public Strategy
{
public:
    TwinsOpeningStrategy(PlayerbotAI* ai,std::string name) : Strategy(ai),name(name) {}
    std::string getName() override { return name; }
private:
    std::string name;
    void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
    void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;
    void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    void InitReactionMultipliers(std::list<Multiplier*>& multipliers) override;
};
}
