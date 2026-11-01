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

    // How many living units of `entry`, a hold_kill row's blocker, stand within kHoldKillRangeYd of the bot and count
    // (CountsAsBlocker, BossTacticsPolicy.h): the "hold kill blockers" value, a grid search at most once a second per
    // bot and entry. Never the bots' "attackers" list: a Portal of Madness is unselectable and never attacks.
    std::uint32_t HoldKillBlockers(PlayerbotAI* ai, std::uint32_t entry);

    // The living, fighting units of `entry` within kHoldKillRangeYd of the bot, a crowd-controlled or evading one
    // included (`eligible` false): the "sole kill candidates" value, one cached grid search per bot and entry, the
    // fight's own units of the entry, never the bot's "attackers" list, which "focus rti targets" collapses to the raid
    // mark.
    std::vector<SoleCandidate> SoleKillCandidates(PlayerbotAI* ai, std::uint32_t entry);

    // The guid of the one living unit of `entry` the fight kills next (0 when nobody of that entry fights yet): the
    // lowest health among the eligible candidates, the lowest guid on a tie (BossTacticsPolicy's PickSoleKill).
    std::uint64_t SoleKillGuid(PlayerbotAI* ai, std::uint32_t entry);

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

    // The held boss entry this DPS bot is on right now, or null: its current target's entry has a hold_kill row whose
    // blocker (a Mad Voidwalker, a Portal of Madness) still lives within kHoldKillRangeYd. Tanks, healers and bots
    // under an explicit attack order never see their target as held.
    Unit* FindBossHoldKill(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

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

    // The listed casts in progress as zones, leaving out those of the casters `bot` holds (a tank the caster is on,
    // or — for a tank_hold caster — a tank whose own target is that caster while it is on a non-tank); `bot` null:
    // all of them.
    std::vector<CastDangerZone> CastDangerZones(PlayerbotAI* ai, const BossTacticsFight& fight, Player* bot);

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

    // Off a boss entry a hold_kill row holds: the kill_first pick if there is one, else the nearest attacker whose own
    // entry is not held, else nothing (attack stopped so the assist rows re-pick once the blocker is dead).
    class BossTacticsHoldKillAction : public AttackAction
    {
    public:
        BossTacticsHoldKillAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics hold kill") {}
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

    // While a listed cast runs, a move whose destination lies in its blast (a reach or chase to a unit standing in it,
    // a heal walk, a follow, a stay spot) would walk the bot back into it: such a move is held (CastHoldsMove).
    // Registered on the combat and non-combat engines: a held bot out of combat itself walks back to its stay there.
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
        // As a reaction the engine would break the bot's cast and stop its walk before Execute runs; Execute does both
        // itself, and only once a walk out is under way.
        bool ShouldReactionInterruptCast() const override { return false; }
        bool ShouldReactionInterruptMovement() const override { return false; }
    };

    class BossTacticsStepBehindAction : public MovementAction
    {
    public:
        BossTacticsStepBehindAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics step behind") {}
        bool isPossible() override;
        bool Execute(Event& event) override;
    };
}
