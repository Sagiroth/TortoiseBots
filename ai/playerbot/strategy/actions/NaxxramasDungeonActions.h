#pragma once
#include "playerbot/PlayerbotAI.h"
#include "DungeonActions.h"
#include "ChangeStrategyAction.h"
#include "UseItemAction.h"
#include "AttackAction.h"

namespace ai
{
    Unit* GetActiveLoathebSporeGuard(PlayerbotAI* ai);
    bool IsLoathebSporeTargetExcluded(PlayerbotAI* ai, Unit* target);
    class GrobbulusCloudEscapeAction : public MovementAction
    {
    public:
        GrobbulusCloudEscapeAction(PlayerbotAI* ai) : MovementAction(ai,"grobbulus cloud escape") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };

    Unit* GetActiveLocustSwarm(PlayerbotAI* ai);
    class LocustSwarmRetreatAction : public MovementAction
    {
    public:
        LocustSwarmRetreatAction(PlayerbotAI* ai) : MovementAction(ai, "locust swarm retreat") {}
        bool isUseful() override;
        bool Execute(Event& event) override;
    };

    Unit* GetMaexxnaRescueTarget(PlayerbotAI* ai);
    bool ShouldDropMaexxnaRescueTarget(PlayerbotAI* ai, Unit* target);
    bool IsMaexxnaRescueTarget(PlayerbotAI* ai, Unit* target);
    class MaexxnaRescueAction : public AttackAction
    {
    public:
        MaexxnaRescueAction(PlayerbotAI* ai) : AttackAction(ai, "maexxna rescue") {}
        Unit* GetTarget() override { return GetMaexxnaRescueTarget(ai); }
        bool isUseful() override;
    };

    class NaxxramasEnableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        NaxxramasEnableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable naxxramas strategy", "+naxxramas") {}
    };

    class NaxxramasDisableDungeonStrategyAction : public ChangeAllStrategyAction
    {
    public:
        NaxxramasDisableDungeonStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable naxxramas strategy", "-naxxramas") {}
    };

    class FourHorsemanEnableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        FourHorsemanEnableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable four horseman fight strategy", "+four horseman") {}
    };

    class FourHorsemanDisableFightStrategyAction : public ChangeAllStrategyAction
    {
    public:
        FourHorsemanDisableFightStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable four horseman fight strategy", "-four horseman") {}
    };

    class FourHorsemanMoveAwayFromVoidZoneAction : public MoveAwayFromHazard
    {
    public:
        FourHorsemanMoveAwayFromVoidZoneAction(PlayerbotAI* ai) : MoveAwayFromHazard(ai, "move away from void zone") {}
    };
}