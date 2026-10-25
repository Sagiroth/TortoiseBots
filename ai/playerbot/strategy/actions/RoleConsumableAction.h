#pragma once
#include "playerbot/strategy/Action.h"
namespace ai
{
class RoleConsumableAction : public Action
{
public:
    RoleConsumableAction(PlayerbotAI* ai) : Action(ai, "role consumable") {}
    bool isUseful() override;
    bool Execute(Event& event) override;
    bool ShouldReactionInterruptCast() const override { return false; }
};
}
