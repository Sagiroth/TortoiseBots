#pragma once
#include "MovementActions.h"
#include "AttackAction.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/PatchwerkOpeningPolicy.h"
namespace ai
{
    bool PatchwerkThreatTank(PlayerbotAI* ai, Unit* target);
    bool PatchwerkDpsOpeningHold(PlayerbotAI* ai);
    bool PatchwerkOpeningBlocksSpell(PlayerbotAI* ai, uint32 spellId, Unit* target);
    class PatchwerkOpeningWindowValue : public CalculatedValue<PatchwerkOpeningWindow>
    {
    public:
        PatchwerkOpeningWindowValue(PlayerbotAI* ai) : CalculatedValue(ai, "patchwerk opening window", 1) {}
        PatchwerkOpeningWindow Calculate() override;
    private:
        PatchwerkOpeningWindow window;
        ObjectGuid bossGuid;
        Group* group = nullptr;
        uint32 instance = 0;
    };
    class PatchwerkOpeningOwnerValue : public CalculatedValue<ObjectGuid>
    {
    public:
        PatchwerkOpeningOwnerValue(PlayerbotAI* ai) : CalculatedValue(ai, "patchwerk opening owner", 1) {}
        ObjectGuid Calculate() override;
    };
    // Cached group discovery also works before the soaker enters combat.
    class PatchwerkSoakBossValue : public CalculatedValue<ObjectGuid>
    {
    public:
        PatchwerkSoakBossValue(PlayerbotAI* ai) : CalculatedValue<ObjectGuid>(ai, "patchwerk soak boss", 1) {}
        ObjectGuid Calculate() override;
    };
    Unit* GetPatchwerkSoakBoss(PlayerbotAI* ai);
    // One-shot handoff of an explicitly ordered main-tank pull after its shot lands.
    void WakePatchwerkPullMain(PlayerbotAI* caller);
    class PatchwerkMainEngageAction : public AttackAction
    {
    public:
        PatchwerkMainEngageAction(PlayerbotAI* ai) : AttackAction(ai, "patchwerk main engage") {}
        bool Execute(Event& event) override;
    };
    class PatchwerkSoakPullerValue : public CalculatedValue<ObjectGuid>
    {
    public:
        PatchwerkSoakPullerValue(PlayerbotAI* ai) : CalculatedValue<ObjectGuid>(ai, "patchwerk soak puller", 1) {}
        ObjectGuid Calculate() override;
    };
    Player* GetPatchwerkSoakPuller(PlayerbotAI* ai);
    bool PatchwerkSoakersReadyForPull(PlayerbotAI* main);
    class PatchwerkSoakStageAction : public MovementAction
    {
    public:
        PatchwerkSoakStageAction(PlayerbotAI* ai) : MovementAction(ai, "patchwerk soak stage") {}
        bool Execute(Event& event) override;
    };
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
