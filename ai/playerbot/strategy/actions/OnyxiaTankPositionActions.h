#pragma once
#include "MovementActions.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
Unit* GetOnyxiaTankPositionTarget(PlayerbotAI* ai);
void UpdateOnyxiaTankOpening(PlayerbotAI* ai);

class OnyxiaTankPositionAction : public MovementAction
{
public:
    OnyxiaTankPositionAction(PlayerbotAI* ai) : MovementAction(ai, "onyxia tank position") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
    void Update();
private:
    bool blockedReported = false, active = false, crossed = false, crossing = false, awaiting = false;
    uint32 lastUpdate = 0, sentAt = 0;
    size_t waypoint = 0;
    std::vector<WorldPosition> openingPath;
    WorldPosition expected;

};

class OnyxiaTankPositionTrigger : public Trigger
{
public:
    OnyxiaTankPositionTrigger(PlayerbotAI* ai) : Trigger(ai, "onyxia tank position needed", 1) {}
    bool IsActive() override { auto* action=ai->GetAiObjectContext()->GetAction("onyxia tank position"); return action && action->isUseful(); }
};

class OnyxiaTankPositionMultiplier : public Multiplier
{
public:
    OnyxiaTankPositionMultiplier(PlayerbotAI* ai) : Multiplier(ai, "onyxia tank position") {}
    float GetValue(Action* action) override;
};
}
