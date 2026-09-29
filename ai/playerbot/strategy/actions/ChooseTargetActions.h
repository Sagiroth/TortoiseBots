#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "AttackAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/playerbot.h"
#include "playerbot/LootObjectStack.h"
#include "ChooseRpgTargetAction.h"

namespace ai
{
    class DpsAoeAction : public AttackAction
    {
    public:
        DpsAoeAction(PlayerbotAI* ai) : AttackAction(ai, "dps aoe") {}
        std::string GetTargetName() override { return "dps aoe target"; }
    };

    class DpsAssistAction : public AttackAction
    {
    public:
        DpsAssistAction(PlayerbotAI* ai) : AttackAction(ai, "dps assist") {}
        std::string GetTargetName() override { return "dps target"; }
        bool isUseful() override;
    };

    class TankAssistAction : public AttackAction
    {
    public:
        TankAssistAction(PlayerbotAI* ai) : AttackAction(ai, "tank assist") {}
        std::string GetTargetName() override { return "tank target"; }
    };

    class AttackAnythingAction : public AttackAction
    {
    private:
        // Repeat-order telemetry: the grind loop can hand out the same mob over and over
        // from the same spot, the signature of an order that never turns into a kill
        // (measured level-1 pool: 66 bots with 20+ orders from one point, the longest
        // series 96 orders over ~50 min with zero kills, and such bots log nothing else,
        // so nothing showed why). The extra "GrindTargetRepeat" row carries the state
        // that separates the two causes - never arrived (not in combat, the mob is not
        // our victim) from arrived and stuck. Throttled: from the third repeat on, at most
        // one row per window, so a bot looping for an hour costs a line a minute instead
        // of one per order.
        ObjectGuid lastGrindTarget;
        uint32 lastGrindOrderMs = 0;
        uint32 lastRepeatLogMs = 0;
        uint32 grindRepeatCount = 0;

        void LogRepeatOrder(Unit* target);

    public:
        AttackAnythingAction(PlayerbotAI* ai) : AttackAction(ai, "attack anything") {}
        std::string GetTargetName() override { return "grind target"; }

        bool isUseful() override;
        bool isPossible() override;
        bool Execute(Event& event) override;
    };

    class AttackLeastHpTargetAction : public AttackAction
    {
    public:
        AttackLeastHpTargetAction(PlayerbotAI* ai) : AttackAction(ai, "attack least hp target") {}
        std::string GetTargetName() override { return "least hp target"; }
    };

    class AttackEnemyPlayerAction : public AttackAction
    {
    public:
        AttackEnemyPlayerAction(PlayerbotAI* ai) : AttackAction(ai, "attack enemy player") {}
        std::string GetTargetName() override { return "enemy player target"; }
        bool isUseful() override;
    };

    class AttackEnemyFlagCarrierAction : public AttackAction
    {
    public:
        AttackEnemyFlagCarrierAction(PlayerbotAI* ai) : AttackAction(ai, "attack enemy flag carrier") {}
        std::string GetTargetName() override { return "enemy flag carrier"; }
        bool isUseful() override;
    };

    class SelectNewTargetAction : public Action
    {
    public:
        SelectNewTargetAction(PlayerbotAI* ai) : Action(ai, "select new target") {}
        bool Execute(Event& event) override;
    };
}
