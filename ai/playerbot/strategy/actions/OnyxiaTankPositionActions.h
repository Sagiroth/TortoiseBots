#pragma once
#include "MovementActions.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
Unit* GetOnyxiaTankPositionTarget(PlayerbotAI* ai);

class OnyxiaTankPositionAction : public MovementAction
{
public:
    OnyxiaTankPositionAction(PlayerbotAI* ai) : MovementAction(ai, "onyxia tank position") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
private:
    bool blockedReported = false;
};

class OnyxiaTankPositionTrigger : public Trigger
{
public:
    OnyxiaTankPositionTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia tank position needed", 1) {}
    bool IsActive() override { OnyxiaTankPositionAction action(ai); return action.isUseful(); }
};

class OnyxiaTankPositionMultiplier : public Multiplier
{
public:
    OnyxiaTankPositionMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia tank position") {}
    float GetValue(Action* action) override;
};
}
