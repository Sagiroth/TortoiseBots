#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BossTacticsPolicy.h"
#include "playerbot/BossTacticsStand.h"
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

    // The auras of one dispel type a unit carries (id + SpellEntry::Dispel), the input of the dispel verdict. Aura
    // iteration only: no grid search, no database.
    void CollectDispelAuras(Unit* member, uint32 dispelType, std::vector<AuraOnMember>& out);
    // Every dispellable aura the unit carries, whatever its type: the input of a cure that removes several types.
    void CollectAllDispelAuras(Unit* member, std::vector<AuraOnMember>& out);
    // The distance to the nearest other living group member (the `clear` test of a dispel_priority row), or a large
    // value when the unit has none: a carrier alone is clear by definition.
    float NearestGroupMemberYd(Unit* member);
    // The dispel_priority verdict for one member, with the aura collection and the group distance taken care of.
    DispelVerdict DispelMemberState(Unit* member, uint32 dispelType, const ActiveBossRows& rows, float defaultClearYd,
                                    float* allyYdOut = nullptr);
    // One bot_events.csv BossTacticsDispel row (throttled per bot and kind): the member a cure was picked for, or the
    // one it was withheld from (a never aura, or a clear row whose carrier is not clear yet).
    void LogDispelPick(PlayerbotAI* ai, Player* bot, const DispelVerdict& verdict, Unit* carrier, float allyYd);
    void LogDispelExcluded(PlayerbotAI* ai, Player* bot, const DispelVerdict& verdict, Unit* carrier, float allyYd);
    // A protected aura applied after an area cure started would be removed by its next tick or pulse: the bot-cast
    // Abolish aura of that type is removed from the member and the bot's own cleansing totem of the type destroyed.
    // With `apply` false the scan only reports whether there is anything to do. Gated by
    // AiPlayerbot.DungeonRunAuraTactics, the map's dispel rows and the "boss tactics" strategy.
    bool SweepBossDispelCures(PlayerbotAI* ai, Player* bot, bool apply);

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

    // The linked_death group ("die together") the fight holds, its members as the pick sees them. `active` is false
    // when the map has no row of the kind. The group and its members come from the table's map rows and a grid search
    // of the group's entries, never from the fight's units: under "focus rti targets" those collapse to the raid mark.
    // Any bot may ask (the AoE density filter leaves a held member out); the pick below is the free DPS bots' only.
    struct BossLinkedDeathScan
    {
        bool active = false;
        std::uint8_t groupId = 0;           // the group (the rows' priority), so one anchor's groups are told apart
        float param = 0.0f;                 // the group's hold percent
        std::uint32_t anchor = 0;           // the group's anchor (the rows' boss_entry)
        std::vector<LinkedMember> members;  // the group's living, fighting units within kHoldKillRangeYd of the bot
        std::vector<Unit*> units;           // the same order: the unit of each member
        std::vector<std::uint32_t> order;   // the release burn's order (empty: the ordinary released pick)
        bool released = false;              // every living member at or below param % (LinkedDeathReleased)
        // ... and the release latch (the group's window_s): a member died while released, so the burn runs on for
        // that long even if a heal or a raise puts a member back above param %.
        bool latched = false;
    };
    BossLinkedDeathScan ScanBossLinkedDeath(PlayerbotAI* ai, Player* bot);

    // The group member this free DPS bot attacks under the linked_death tactic, or null. Gated by
    // AiPlayerbot.DungeonRunLinkedDeath and the "boss tactics" strategy; null while the group is released (every living
    // member at or below param %, when the ordinary pick burns them together), and for tanks, healers and bots under an
    // explicit attack order.
    Unit* FindBossLinkedDeath(PlayerbotAI* ai, Player* bot);

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

    // Removes what an already-running area cure would do to a protected aura (SweepBossDispelCures).
    class BossTacticsDispelSweepAction : public Action
    {
    public:
        BossTacticsDispelSweepAction(PlayerbotAI* ai) : Action(ai, "boss tactics dispel sweep") {}
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

    // On a linked_death group member that is not the equalise pick (another member stands higher): switch to the pick,
    // so the damage is spread instead of being left on one member until it dies.
    class BossTacticsLinkedDeathAction : public AttackAction
    {
    public:
        BossTacticsLinkedDeathAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics linked death") {}
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

    // A cure that would remove a `never` aura, or an un-clear `clear` one, is stood down: the single-target cure on an
    // excluded member, and the area/periodic cures (Abolish Disease, the cleansing totems, Stoneform) while any living
    // group member within kDispelSafetyYd carries such an aura of the type. Combat only.
    class BossTacticsDispelHoldMultiplier : public Multiplier
    {
    public:
        BossTacticsDispelHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics dispel hold") {}
        float GetValue(Action* action) override;
    };

    // A damaging trap's circle over a move's destination holds that move the way
    // a listed cast does: a bot does not walk back into fire it just stepped out
    // of. The stay and follow spots are judged as the destinations they are; a
    // reach is judged at the point where it stops (the target moved toward the
    // bot by its stop range), not at the target's centre. Attacks are never
    // held (target pick-up and engagement must run) and the hold is combat-only.
    // The step-out itself (avoid aoe) is never held, or it could not leave.
    // Registered on the always-on combat strategies (avoid aoe, dungeon), not on
    // a boss-tactics one: a trap fire is not gated behind an encounter.
    class TrapHoldMultiplier : public Multiplier
    {
    public:
        TrapHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "trap hold") {}
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

    // The bot walks to its seat of the stand set that applies now (ai_boss_stand), once.
    class BossTacticsStandAction : public MovementAction
    {
    public:
        BossTacticsStandAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics stand") {}
        bool isPossible() override;
        bool Execute(Event& event) override;

    private:
        // The last seat the walk was started for: a bot already walking there is not re-pathed (MoveTo relaunches
        // the spline on every call).
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool hasLast = false;
    };

    class BossTacticsStepBehindAction : public MovementAction
    {
    public:
        BossTacticsStepBehindAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics step behind") {}
        bool isPossible() override;
        bool Execute(Event& event) override;
    };
}
