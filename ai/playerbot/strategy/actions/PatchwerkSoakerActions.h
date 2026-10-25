#pragma once
#include "MovementActions.h"
namespace ai
{
    Unit* GetPatchwerkBoss(PlayerbotAI* ai);
    Unit* GetAssignedPatchwerkTankTarget(PlayerbotAI* ai);
    bool PatchwerkSoakerAssigned(PlayerbotAI* ai);
    bool PatchwerkNeedsMeleeHold(PlayerbotAI* ai, Unit* boss);
    class PatchwerkMeleeRetreatAction : public MovementAction
    {
    public:
        PatchwerkMeleeRetreatAction(PlayerbotAI* ai) : MovementAction(ai, "patchwerk melee retreat") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
}
