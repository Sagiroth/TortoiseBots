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

    // A creature in the fight casting a spell an avoid_cast row lists, and the radius to keep from it.
    struct BossCastDanger
    {
        Unit* caster = nullptr;
        float radius = 0.0f;
    };

    // The radius `unit` must be kept at right now (its current cast is listed), or 0.
    float CastDangerOf(const BossTacticsFight& fight, Unit* unit);

    // Whether a point lies inside the radius of any listed cast in progress.
    bool InsideCastDanger(const BossTacticsFight& fight, float x, float y, float z);

    // The nearest listed cast this bot stands inside and has to leave (never a tank), or no caster.
    BossCastDanger FindBossCastDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

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

    // While a listed cast runs, a reach towards its caster would walk a melee bot straight back into the blast, and
    // a stay whose spot lies in the blast would walk a held bot back onto it.
    class BossTacticsCastHoldMultiplier : public Multiplier
    {
    public:
        BossTacticsCastHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics cast hold") {}
        float GetValue(Action* action) override;
    };

    class BossTacticsAvoidCastAction : public MovementAction
    {
    public:
        BossTacticsAvoidCastAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics avoid cast") {}
        bool isUseful() override;
        bool isPossible() override;
        bool Execute(Event& event) override;
    };

    class BossTacticsStepBehindAction : public MovementAction
    {
    public:
        BossTacticsStepBehindAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics step behind") {}
        bool isPossible() override;
        bool Execute(Event& event) override;
    };
}
