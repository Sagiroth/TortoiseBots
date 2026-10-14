#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BossTacticsPolicy.h"
#include "ChangeStrategyAction.h"
#include "AttackAction.h"
#include "MovementActions.h"

namespace ai
{
    class BossTacticsEnableStrategyAction : public ChangeAllStrategyAction
    {
    public:
        BossTacticsEnableStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "enable boss tactics strategy", "+boss tactics") {}
    };

    class BossTacticsDisableStrategyAction : public ChangeAllStrategyAction
    {
    public:
        BossTacticsDisableStrategyAction(PlayerbotAI* ai) : ChangeAllStrategyAction(ai, "disable boss tactics strategy", "-boss tactics") {}
    };

    // The fight as the tactics see it: the live attackers (cached value, no grid
    // sweep) and the rows of every boss entry among them.
    struct BossTacticsFight
    {
        std::vector<Unit*> units;
        ActiveBossRows rows;
    };
    BossTacticsFight ScanBossTactics(PlayerbotAI* ai, Player* bot);

    // A listed spell being cast right now, by a creature in the fight.
    struct BossInterruptScan
    {
        Unit* caster = nullptr;
        const char* spell = nullptr; // the interrupt this bot can use on it
    };
    BossInterruptScan FindBossInterrupt(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // The kill_first creature this DPS bot should be on, or null (tanks, healers
    // and bots under an explicit attack order never get one).
    Unit* FindBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // The boss whose front this bot should leave, or null.
    Unit* FindBossFrontDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    class BossTacticsInterruptAction : public Action
    {
    public:
        BossTacticsInterruptAction(PlayerbotAI* ai) : Action(ai, "boss tactics interrupt") {}
        bool Execute(Event& event) override;
    };

    class BossTacticsKillFirstAction : public AttackAction
    {
    public:
        BossTacticsKillFirstAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics kill first") {}
        bool Execute(Event& event) override;
    };

    // Once on the kill-first creature, the generic assist/AoE re-pick would swing
    // the bot back to the boss the next tick.
    class BossTacticsHoldMultiplier : public Multiplier
    {
    public:
        BossTacticsHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics hold") {}
        float GetValue(Action* action) override;
    };

    class BossTacticsStepBehindAction : public MovementAction
    {
    public:
        BossTacticsStepBehindAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics step behind") {}
        bool isPossible() override;
        bool Execute(Event& event) override;
    };
}
