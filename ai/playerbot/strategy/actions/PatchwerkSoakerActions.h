#pragma once
#include "MovementActions.h"
#include "AttackAction.h"
#include "playerbot/strategy/Value.h"
namespace ai
{
    // Cached group discovery also works before the soaker enters combat.
    class PatchwerkSoakBossValue : public CalculatedValue<ObjectGuid>
    {
    public:
        PatchwerkSoakBossValue(PlayerbotAI* ai) : CalculatedValue<ObjectGuid>(ai, "patchwerk soak boss", 1) {}
        ObjectGuid Calculate() override;
    };
    Unit* GetPatchwerkSoakBoss(PlayerbotAI* ai);
    class PatchwerkSoakEngageAction : public AttackAction
    {
    public:
        PatchwerkSoakEngageAction(PlayerbotAI* ai) : AttackAction(ai, "patchwerk soak engage") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };
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
