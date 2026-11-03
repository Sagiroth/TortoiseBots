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
    // The fight scan's one-slot cache belongs to one AI update: an explicit command runs between updates on the same
    // world thread with the same update tick, so ScanBossTactics only caches while a scope is open, and UpdateAI
    // clears the slot when it returns. One scope per UpdateAI, the whole body.
    class BossTacticsScanScope
    {
    public:
        BossTacticsScanScope();
        ~BossTacticsScanScope();
        BossTacticsScanScope(const BossTacticsScanScope&) = delete;
        BossTacticsScanScope& operator=(const BossTacticsScanScope&) = delete;
    };

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
    // The same hold judged again at cast time (PlayerbotAI::CastSpell): the target may have gained a protected aura
    // after the action was chosen. True when the cast must not be issued because the spell removes, by any of the
    // dispel types read from its effects, a protected aura the target carries, or because the target is a unit the
    // gate cannot place (not the bot, a group member or a group member's pet). Gated like SweepBossDispelCures.
    bool DispelCastBlocked(PlayerbotAI* ai, Unit* target, std::uint32_t spellId);

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

    // Whether a free DPS bot leaves `unit` alone right now under a dps_hold_after row of its entry: the fight-wide
    // hold state was started by the row's engage, band, spell or blink trigger and has not run out (DpsHoldTick).
    // Gated by AiPlayerbot.DungeonRunAuraTactics, HasAuraTacticMap and the "boss tactics" strategy; null/false for
    // tanks, healers and bots under an explicit attack order. `trigger` (optional) names the trigger of the hold.
    bool IsDpsHeld(PlayerbotAI* ai, Player* bot, Unit* unit, DpsHoldTrigger* trigger = nullptr);

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

    // The freeze_on_aura row's aura this bot carries, or 0: the bot must stand still and do nothing while it lasts
    // (BossTacticsPolicy's FreezeHoldsAction).
    std::uint32_t FreezeAuraOnBot(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // One read of the bot's freeze episode: starts it on the rising edge, tracks the drift from the start point and,
    // when the aura is gone, writes the `BossTacticsFreeze` row (aura, ms held, moved_yd) and forgets it. The trigger
    // calls it every tick, so an aura that ends between two engine ticks still logs.
    void FreezeEpisodeTick(PlayerbotAI* ai, Player* bot, std::uint32_t aura);

    // Whether this is the freeze episode's first stop: the action then calls AttackStop, CastStop and StopMoving once,
    // and never again for that aura.
    bool FreezeStopDue(PlayerbotAI* ai, Player* bot, std::uint32_t aura);

    // The flee_on_aura row this bot's carried aura activates, or null. Rows an inactive boss leaves out are not in the
    // fight (ScanBossTactics), so a row of a boss that is not there never moves anyone.
    const BossTacticRow* ActiveFleeRow(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // Whether the fight has an active flee_on_aura row for `spellId`: the hard-coded bomb runout stands down for that
    // aura, so one carrier is never moved by two actions.
    bool FleeRowListsAura(PlayerbotAI* ai, Player* bot, std::uint32_t spellId);

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

    // A free DPS bot on a subject a dps_hold_after row holds stops attacking it: the kill_first pick if there is one
    // (the adds stay allowed), else the target is dropped and the attack stopped until the hold runs out.
    class BossTacticsDpsHoldAction : public AttackAction
    {
    public:
        BossTacticsDpsHoldAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics dps hold") {}
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

    // The carrier of a freeze_on_aura row stands where it is and starts nothing: its attack stops, its cast breaks and
    // its walk ends once, on the aura's rising edge. The multiplier below then holds every other action while it
    // lasts, so the bot neither moves nor heals (threat) until the aura is gone.
    class BossTacticsFreezeAction : public Action
    {
    public:
        BossTacticsFreezeAction(PlayerbotAI* ai) : Action(ai, "boss tactics freeze") {}
        bool Execute(Event& event) override;
    };

    // The carrier of a flee_on_aura row leaves the group (`away`) or walks to the main tank (`to_tank`). Both modes
    // reuse the runout's move body; `away` keeps a living healer in reach (RunOutFromGroup).
    class BossTacticsFleeAction : public MovementAction
    {
    public:
        BossTacticsFleeAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics flee") {}
        bool Execute(Event& event) override;
        bool isPossible() override { return MovementAction::isPossible() && ai->CanMove(); }
    };

    // Frozen: every action but the freeze action itself is held (FreezeHoldsAction). Registered on the combat,
    // non-combat and reaction engines, because a bot's heal, its walk and its own cast all run outside combat.
    class BossTacticsFreezeMultiplier : public Multiplier
    {
    public:
        BossTacticsFreezeMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics freeze") {}
        float GetValue(Action* action) override;
    };

    // AiPlayerbot.DungeonRunCharmYield: an area action that would land within kCharmedAllyAreaYd of a creature-charmed
    // group member is stood down. Not a boss-tactics rule: the key alone decides, on any map, so the class is
    // registered on the always-on combat strategies (the TrapHoldMultiplier placement), not on BossTacticsStrategy.
    class CharmedAllyHoldMultiplier : public Multiplier
    {
    public:
        CharmedAllyHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "charmed ally hold") {}
        float GetValue(Action* action) override;
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

    // The use_object kind's clicker side: one claimed bot walks to the ready object nearest the boss, holds there and
    // uses the object once the row is due and the boss stands still inside the row's range. Every bot computes the same
    // claim from the same group roster (UseObjectPolicy.h); `valid` is true only for the claimed bot and only from the
    // row's lead onwards, so no other bot's trigger fires.
    struct UseObjectTask
    {
        bool valid = false;
        bool logClaim = false;  // the claim line has not been written yet
        Unit* boss = nullptr;
        GameObject* object = nullptr;  // null when no object of the row's entry is ready right now
        const BossTacticRow* row = nullptr;
        bool due = false;
        bool attemptReady = false;
        bool bossMoving = false;
        float bossToObject = 0.0f;
        std::uint32_t msToDue = 0;
        std::uint32_t sinceMs = 0;  // since the last click, or since the boss was first seen before any
    };
    UseObjectTask FindBossUseObject(PlayerbotAI* ai, Player* bot);
    void NoteBossUseObjectClaimLogged(Unit* boss, const BossTacticRow* row);

    // The mode tank_anchor side: the point the tank the boss is on walks it to, 0.4 * the row's range short of the
    // object. `valid` only while the walk is still wanted and the tank is farther than 3 yd from the point.
    struct UseObjectAnchor
    {
        bool valid = false;
        Unit* boss = nullptr;
        GameObject* object = nullptr;
        const BossTacticRow* row = nullptr;
        float bossToObject = 0.0f;
        float tankDistance = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
    };
    UseObjectAnchor FindBossUseObjectAnchor(PlayerbotAI* ai, Player* bot);

    class BossTacticsUseObjectAction : public MovementAction
    {
    public:
        BossTacticsUseObjectAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics use object") {}
        bool isPossible() override;
        bool Execute(Event& event) override;

    private:
        // The walk in was started for this spot: MoveTo relaunches the spline on every call, so a bot already on its way
        // is left alone (the stand action's pattern).
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool hasLast = false;
    };

    class BossTacticsObjectAnchorAction : public MovementAction
    {
    public:
        BossTacticsObjectAnchorAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics object anchor") {}
        bool isPossible() override;
        bool Execute(Event& event) override;
    };

    // A taunt_after_cast row (mode until_tank) whose subject stopped being on a tank, for the one bot the row's raid
    // order names (`priority`, the first alive at or after it): `valid` is true only while that bot is the one asking
    // and the deadline the rising edge opened has not run out. `sinceMs` is how long ago it opened, `deadlineMs` the
    // row's param. blink and the spell triggers stay unimplemented: the loader accepts those rows, this scan ignores
    // them.
    struct BossTauntAfter
    {
        bool valid = false;
        Unit* subject = nullptr;
        const BossTacticRow* row = nullptr;
        int tankIndex = -1;          // the bot's slot in the raid tank order
        std::uint32_t deadlineMs = 0;
        std::uint32_t sinceMs = 0;   // since the subject's victim stopped being a tank
    };
    // One read of the taunt_after_cast rows of the fight: opens the episode on the rising edge, closes and logs it
    // when the subject is on a tank again or the deadline ran out (`BossTacticsTauntAfter`). Gated by
    // AiPlayerbot.DungeonRunAuraTactics and the "boss tactics" strategy; the episode state is the fight's, shared by
    // every bot of the instance, so a tank taking over mid-episode continues it.
    BossTauntAfter FindBossTauntAfter(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);
    // The taunt of the episode was cast: its outcome row then says taunted=1.
    void NoteBossTauntAfterCast(PlayerbotAI* ai, Player* bot, const BossTauntAfter& task);

    // The row's tank attacks the subject and casts its class taunt (`taunt`/`growl`) as soon as it can.
    class BossTacticsTauntAfterAction : public AttackAction
    {
    public:
        BossTacticsTauntAfterAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics taunt after") {}
        bool Execute(Event& event) override;
    };
}
