#pragma once
#include "MovementActions.h"
#include "playerbot/GeddonBombPolicy.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/Trigger.h"

namespace ai
{
class GeddonBombStateValue : public ManualSetValue<GeddonBombState&>
{
public:
    GeddonBombStateValue(PlayerbotAI* ai) : ManualSetValue<GeddonBombState&>(ai, state, "geddon bomb state") {}
private:
    GeddonBombState state;
};
bool HasGeddonBombSpot(PlayerbotAI* ai);
bool UpdateGeddonBombMovement(PlayerbotAI* ai);
class GeddonBombMovementTrigger : public Trigger
{
public:
    GeddonBombMovementTrigger(PlayerbotAI* ai) : Trigger(ai, "geddon bomb movement", 1) {}
    bool IsActive() override { return UpdateGeddonBombMovement(ai); }
};
class GeddonBombMoveAction : public MovementAction
{
public:
    GeddonBombMoveAction(PlayerbotAI* ai) : MovementAction(ai, "geddon bomb move") {}
    bool isUseful() override { return UpdateGeddonBombMovement(ai); }
    bool isPossible() override { return true; }
    bool isUsefulWhenStunned() override { return true; }
    bool Execute(Event& event) override;
};
}
