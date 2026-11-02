#pragma once
#include "AttackAction.h"
namespace ai {
bool FankrissSingleTargetActive(PlayerbotAI* ai);
bool FankrissAoeSpell(SpellEntry const* spell);
bool FankrissBlocksAoe(PlayerbotAI* ai, uint32 spell);
Unit* GetFankrissRescueTarget(PlayerbotAI* ai);
Unit* GetFankrissDpsTarget(PlayerbotAI* ai);
bool FankrissDpsMayAttack(PlayerbotAI* ai, Unit* target);
void UpdateFankrissFocus(PlayerbotAI* ai);
Unit* GetFankrissFocusSpawn(PlayerbotAI* ai);
bool FankrissFocusEnabled(PlayerbotAI* ai);
// The single-target rule and the gauntlet ignore apply only to DPS that run a Fankriss role.
bool FankrissDpsOptedIn(PlayerbotAI* ai);
bool FankrissBurnSpawn(PlayerbotAI* ai, Unit* target);
bool RetainFankrissFocusTarget(PlayerbotAI* ai,Unit* target);
class FankrissFocusAction : public AttackAction {
public:
 FankrissFocusAction(PlayerbotAI* ai):AttackAction(ai,"fankriss focus spawn"){}
 void Update();
 bool ShouldReactionInterruptCast() const override {return true;}
 bool isUseful() override;
 bool Execute(Event&) override;
private:
 uint32 lastUpdate=0, lastReport=0;
};
class FankrissFocusTrigger : public Trigger {
public:
 FankrissFocusTrigger(PlayerbotAI* ai):Trigger(ai,"fankriss focus needed",1){}
 bool IsActive() override {return ai->GetAiObjectContext()->GetAction("fankriss focus spawn")->isUseful();}
};
class FankrissControlAction : public Action {
public:
 FankrissControlAction(PlayerbotAI* ai):Action(ai,"fankriss control spawn"){}
 bool isUseful() override;
 bool Execute(Event&) override;
};
class FankrissControlTrigger : public Trigger {
public:
 FankrissControlTrigger(PlayerbotAI* ai):Trigger(ai,"fankriss control needed",1){}
 bool IsActive() override {return ai->GetAiObjectContext()->GetAction("fankriss control spawn")->isUseful();}
};
}
