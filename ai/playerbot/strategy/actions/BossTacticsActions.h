#pragma once
#include <limits>
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BossTacticsPolicy.h"
#include "playerbot/CombatSpreadPolicy.h"
#include "Objects/DynamicObject.h"
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
    // A bot on battle duty: not a tank, not a healer. The hold's role gate, for the clauses an explicit attack order
    // may lift (IsFreeDps) and for those it may not (the area and attacker-debuff veto).
    bool IsBattleDps(Player* bot);
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

    // How many living blockers of a gate_hold row fight within `radius` yd of the bot (in combat and not evading:
    // CountsAsFightingBlocker): the "fighting blockers" value, one cached grid search per bot, entry and radius.
    std::uint32_t GateHoldFighters(PlayerbotAI* ai, std::uint32_t entry, float radius);

    // Whether `unit` is spared right now by a gate_hold row of the fight: its health is at or below the row's gate %
    // and more than the row's `priority` blockers still fight (GateHeld). Gated by AiPlayerbot.DungeonRunGateHold; the
    // callers apply the role gate (free DPS) and the explicit-order rule, as for hold_kill.
    bool GateHoldsUnit(PlayerbotAI* ai, const ActiveBossRows& rows, Unit* unit);

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

    // The kill_first creature this DPS bot should be on, or null. Tanks and healers never get one; a bot under an
    // explicit attack order the run issued gets only the carrier override (KillFirstPickMode::Carrier): a Delusions
    // carrier's Shade of Jin'do within its reach (t1105h J1). A player's order gets none.
    Unit* FindBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // The carrier hold, whatever the bot's orders: the nearest live unit of an aura-gated kill_first row whose aura
    // this DPS bot carries, within kCarrierHoldReachYd (the free pick's own range). The run's own focus skips such a bot
    // (DungeonRun::CarrierFocusSkip, FocusSkipsBot), so its refocus does not pull it back off its Shade.
    Unit* FindBossCarrierPick(PlayerbotAI* ai, Player* bot);

    // The carrier strike's Shade, whatever the bot's role or orders: the nearest pickable unit of an open aura-gated
    // kill_first row within kCarrierHoldReachYd, or null. The engine only reads it while
    // AiPlayerbot.DungeonRunCarrierStrike is on; the trigger and the action both go through here.
    Unit* FindBossCarrierStrikePick(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // The off-tank's healer-cover latch (AiPlayerbot.DungeonRunShadeHealerCover): the strike trigger updates it, and
    // FindBossCarrierStrikePick keeps the latched Shade as the pick while ShadeCoverKeepsLatched holds.
    ShadeCoverLatch GetShadeCoverLatch(Player* bot);
    void SetShadeCoverLatch(Player* bot, const ShadeCoverLatch& latch);

    // Whether this bot carries the aura of an open aura-gated kill_first row right now (a Delusions carrier), whatever
    // its target: such a bot can hurt only the row's unit, so the totem focus never sends it at a totem and counts no
    // volley of it (t1106b). False on a map without rows and for a bot that lacks the aura (the fight scan drops a
    // gated row then).
    bool CarriesAuraGatedKillFirst(PlayerbotAI* ai, Player* bot);

    // The carrier no-switch row (t1106b): a Delusions carrier with a Shade of the fight and no switch to it, with the
    // reason and what the bot was striking instead. At most one row per bot per 5 s; a no-op without bot_events.csv,
    // for a bot that is no battle DPS, and for a bot that carries no gated row. `keptReason` overrides the read
    // (BossTacticsKillFirstAction's stale current target); nullptr derives it from the fight read.
    void LogCarrierNoSwitch(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, Unit* pick, Unit* current,
        const char* keptReason = nullptr);

    // Whether `unit`'s victim is a healer of the bot's group (the bot itself included): a Shade of Jin'do on a healer.
    bool ShadeOnGroupHealer(Player* bot, Unit* unit);

    // The Shade cover row (walk 56 F3, no flag): run by a Delusions carrier, it names every cursed damage dealer, and
    // every cursed off-tank that does not hold the boss, that is not on a Shade it can see attacking a healer of the
    // group, with the reason word (ShadeCoverMissReason). The row is logged under the member it names, at most once per
    // (member, Shade) per 5 s and only after a 2 s miss. A no-op without bot_events.csv and for an uncursed bot.
    void LogShadeCover(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // The taunt gate (AiPlayerbot.DungeonRunTauntGate): whether a taunt at `target` must not go out, because it is a
    // needs_aura subject (a Shade of Jin'do, which only a carrier may damage) or a taunt-immune creature. Logs one
    // BossTacticsTauntGate row per bot per 5 s while it refuses. `spell` is the taunt being tried, for the row.
    bool TauntRefusedByGate(PlayerbotAI* ai, Player* bot, Unit* target, const char* spell);

    // A generic taunt's cast-time check (TauntHeldTargetPolicy.h): true when `target`'s current victim is a player other
    // than `bot` that PlayerbotAI::IsTank reads as a tank. Builds the victim facts in call order (TauntHeldInputs) and
    // answers through TauntStandsDownForTank. Only the class taunt actions call it; TauntRefusedByGate and
    // the boss-tactics taunts never do, so a planned tank swap still taunts off another tank.
    bool TauntTargetHeldByOtherTank(Player* bot, Unit* target);

    // The carrier strike verdict row: one BossTacticsCarrierStrike row whenever the verdict changes, and at most one
    // every 5 s while it stays the same. `spell` is the strike the action would use ("<id>", "melee" or "none").
    // A no-op without bot_events.csv.
    void LogCarrierStrike(PlayerbotAI* ai, Player* bot, CarrierStrikeVerdict verdict, CarrierStrikeRole role,
        Unit* shade, float shadeYd, std::uint8_t lowestPct, std::uint8_t tankPct, std::uint8_t otherHealers,
        std::uint8_t manaPct, const char* spell);

    // tank_ignore (t1105h J2): whether this bot, a tank, leaves the target alone because a boss whose tank_ignore row
    // names the target's entry fights within kBossTacticsEngageRange of it (alive, not evading, engaged with our group).
    // The attack pickers (PossibleAttackTargetsValue::IsValid: the tank target, the attacker-of-non-tank taunt, the
    // snare taunt) and the taunt_after_cast taunt read it. False for any bot that is no tank, and on a map without rows.
    bool TankIgnoresTarget(PlayerbotAI* ai, Player* bot, Unit* target);

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

    // A cursed healer or off-tank strikes its own Shade with the class's shortest damaging spell, or with Attack when
    // no spell can be cast (and always for an off-tank). No order or attack target is recorded, so when the guard
    // fails or the curse ends the heal rows win again and the Shade is dropped by the aura gate.
    class BossTacticsCarrierStrikeAction : public AttackAction
    {
    public:
        BossTacticsCarrierStrikeAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics carrier strike") {}
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
    // group member is stood down, and so is a harmful single-target action (attack, damage/interrupt/CC cast, taunt,
    // pet order) aimed at the member itself; healing stays allowed. Not a boss-tactics rule: the key alone decides, on
    // any map, so the class is registered on the always-on combat strategies (the TrapHoldMultiplier placement), not
    // on BossTacticsStrategy.
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

    // The swap_on_aura kind's other-tank side: the tank of the row's rotation that takes the boss at the row's stack
    // count (TankSwapPolicy.h). `valid` is true only for that tank, and only for a swap the row calls for now.
    struct BossTankSwapTask
    {
        bool valid = false;
        Unit* boss = nullptr;
        Unit* victim = nullptr;  // the tank the boss is on, one of the rotation
        const BossTacticRow* row = nullptr;
        int victimIndex = -1;    // its place in the raid tank order
        int botIndex = -1;       // the bot's place (the one that takes over)
        int victimStacks = 0;
    };
    BossTankSwapTask FindBossTankSwap(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // Whether the fight's swap record names `bot` as the tank the last swap moved off `boss`: it stands down (the
    // stop action and the hold multiplier) while TankSwapStandsDown says so (the record's window, its own stacks,
    // and the boss still on the new tank). HeldSwapBoss clears it for an explicit player order on the boss.
    bool TankSwapHoldsTank(Player* bot, Unit* boss, const BossTacticRow& row, int stacks);

    // The swap_on_aura boss this tank was moved off and still stands on (its victim or its current target), or null
    // when the stand-down ended or it is not on that boss: the stop action and its trigger share this rule.
    Unit* TankSwapStandDownBoss(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // Records the swap the bot just did (the boss came to it, the old tank stood down) and writes the
    // `BossTacticsTankSwap` row (taunt=<boss guid>, stacks=<n>, from=<guid>).
    void NoteBossTankSwapCast(PlayerbotAI* ai, Player* bot, const BossTankSwapTask& task);

    class BossTacticsTankSwapAction : public AttackAction
    {
    public:
        BossTacticsTankSwapAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics tank swap") {}
        bool Execute(Event& event) override;
    };

    // The swapped-off tank side of the swap: it drops the boss (attack, cast, pick) the tick still stands on it, so
    // no class keeps building threat there; the hold multiplier stands down every action aimed at that boss until the
    // stand-down ends.
    class BossTacticsTankSwapHoldAction : public Action
    {
    public:
        BossTacticsTankSwapHoldAction(PlayerbotAI* ai) : Action(ai, "boss tactics tank swap hold") {}
        bool Execute(Event& event) override;
    };

    // The seek_aura fight's clock and gather state, as the run leader reads it: the next Blood Siphon's due time, the
    // dose window, the cycle count and the gather (the source it stands around and whether the burn may start). One
    // record per (instance, boss, row) is shared by every bot of the instance (SeekCycleSnapshot).
    struct SeekCycleView
    {
        bool valid = false;
        std::uint32_t dueMs = 0;    // WorldTimer::getMSTime of the next cycle spell (0 = no cycle yet)
        std::uint32_t dueInMs = 0;  // max(0, dueMs - now)
        bool windowOpen = false;
        std::uint32_t cycleNo = 0;
        // The Cause Insanity reset clock (DungeonRunThreatResetWindow): when the fight's last new charm was seen, and
        // the fight's combat start (the first reset is counted from it plus an offset).
        std::uint32_t lastResetMs = 0, engageMs = 0;
        bool gathering = false;     // a source stands at the spot and the gather is under way
        bool gatherReady = false;
        int missing = 0;            // seekers still outside the gather radius
        // The latest accepted siphon's dose counts, read one second after the sighting: not group members carrying the
        // dummy aura 24323 (dosed) and 24322 (not). siphonCycle 0: none read yet.
        std::uint32_t siphonCycle = 0;
        int siphonDosed = 0;
        int siphonUndosed = 0;
    };
    bool SeekCycleSnapshot(std::uint32_t instanceId, std::uint64_t bossGuid, SeekCycleView& out);

    // The puller of this instance's open pulled-Son meet, or 0 (DungeonRunSeekMeetNoHeal). The heal-target value reads it
    // once per pick and skips that bot (SeekMeetHealHeld). Returns 0 before any lock with the key off or no meet open on
    // the server (an atomic count); otherwise one locked read of the meet's receiver, shot time and puller together.
    std::uint64_t SeekMeetHealPuller(std::uint32_t instanceId);

    // The seek_aura fight records' handoff across `.bot run reloadplans`: records whose stable row identity still
    // exists in the new tactic table with the same cycle spell, cycle length and dose window keep their engage, last
    // siphon and window; records of removed or retimed rows are dropped. The old table may then be freed (the records
    // copy every column they read and keep no row pointer).
    void ReconcileSeekFightState(const BossTacticTable& next);

    // What this bot does about the row right now: walk into the row's cloud (the source already died), or walk to the
    // gather around the dying source. `phase` is the log's own word (cloud / gather / hold).
    struct SeekTask
    {
        enum class Phase
        {
            None = 0,
            Cloud = 1,  // walk into a cloud the row's source already left
            Gather = 2, // walk to the gather around the source before it dies
        };
        bool valid = false;
        Phase phase = Phase::None;
        const BossTacticRow* row = nullptr;
        Unit* boss = nullptr;
        Unit* source = nullptr;         // the living source the gather stands around (gather only)
        DynamicObject* cloud = nullptr; // the cloud the bot walks into (cloud only)
        float x = 0.0f, y = 0.0f, z = 0.0f; // the point the bot walks to
        float dist = 0.0f;              // bot to the cloud's centre, or to the source
        bool atSpot = false;            // the gather: already inside the radius
        bool hasAura = false;
        std::uint32_t dueInMs = 0;
        std::uint32_t cycleNo = 0;
    };
    SeekTask FindBossSeek(PlayerbotAI* ai, Player* bot);
    // Whether the bot is in a seek phase right now: the move hold and the raid spread stand-down read it.
    bool BossSeeking(PlayerbotAI* ai, Player* bot);

    // Whether this hunter's or warlock's pet has to be out of the siphon now: a hunter's from 5 s before the due time
    // until 2 s after it (and while the siphon's dummy aura is on the bot), a warlock's for the whole fight (one
    // dismiss at the engage, no resummon). True only in a live seek_aura fight.
    bool BossSeekPetMustLeave(PlayerbotAI* ai, Player* bot);
    // Whether this shaman's totems have to be out of the siphon now: the same window as the pets (from 5 s before the
    // due time until 2 s after it, and while the siphon's dummy aura is on the bot). True only in a live seek_aura
    // fight.
    bool BossSeekTotemsMustLeave(PlayerbotAI* ai, Player* bot);
    // The shaman's four totems are unsummoned at once (the core's Unit::UnsummonAllTotems).
    class BossTacticsSeekTotemAction : public Action
    {
    public:
        BossTacticsSeekTotemAction(PlayerbotAI* ai) : Action(ai, "boss tactics seek totem") {}
        bool Execute(Event& event) override;
    };
    // A pet is dismissed at once; a hunter's pet waits in its current slot for the call back after the window, a
    // warlock's demon is not kept.
    class BossTacticsSeekPetAction : public Action
    {
    public:
        BossTacticsSeekPetAction(PlayerbotAI* ai) : Action(ai, "boss tactics seek pet") {}
        bool Execute(Event& event) override;
    };
    // A pet is called, revived or summoned back, or a totem placed, while it would stand in the siphon: the action is
    // held (each would be one more heal). Only the seek_aura fight reads it.
    class BossTacticsSeekPetHoldMultiplier : public Multiplier
    {
    public:
        BossTacticsSeekPetHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics seek pet hold") {}
        float GetValue(Action* action) override;
    };
    // The walk-out focus (DungeonRunSeekWalkoutFocus): while this bot is the live walk-out puller, at or above 40 %
    // health, and its shot has not landed, every combat-engine action but the walk-out ("boss tactics seek tank") and
    // the pet's dismissal ("boss tactics seek pet") is held (WalkoutFocusHolds; reaction actions still run), so
    // "follow", "hunter step out" or "wait for attack" no longer take the puller's ticks at the stand. One
    // `seek pull: walk-out preempted action=<name>` row per walk-out.
    class BossTacticsWalkoutFocusMultiplier : public Multiplier
    {
    public:
        BossTacticsWalkoutFocusMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics walkout focus") {}
        float GetValue(Action* action) override;
    };

    // Whether this bot is the live, unshot walk-out puller of its instance's task (the walk-out focus's record read:
    // recorded as the puller, alive, not charmed, inside the 25 s, the shot not landed; WalkoutHoldsCombat). False,
    // without a lock, while no task runs or off a seek_aura map. `combat end` (DungeonRunSeekWalkoutHoldCombat) and
    // UpdateFaceTarget (DungeonRunSeekWalkoutFaceCast) read it.
    bool IsLiveWalkoutPuller(Player* bot);

    // The third off-tank's own seek_aura source: while it drags one, the generic loose-add pickup must not pull it
    // off. False on every other bot, row or target.
    bool IsSeekSourceForTankPickup(PlayerbotAI* ai, Player* bot, Unit* unit);

    // Whether `dyn` is a seek_aura row's cloud: the aura itself, or the trigger spell of its persistent area effect.
    // The ground-hazard readers skip it while the row is live, so the raid stands in the poison on purpose.
    bool IsSoughtCloudFor(PlayerbotAI* ai, Player* bot, DynamicObject* dyn);

    // The anchor this bot is held in right now (kind `anchor`, BossAnchorPolicy.h): a row of a boss that is in the bot's
    // fight (among its side's attackers, alive, in combat, not evading), with the bot's own radius (tanks may have a
    // tighter one). False on a map without anchor rows, without "boss tactics", or when no anchored boss fights.
    // Every AI update: a walk-out puller's pet, held passive at its heels while the task or its walk back ran, gets its
    // react state and its stay/follow command back once nothing holds it any more (any end, on any path).
    void ReleaseWalkoutPet(Player* bot);
    bool LiveBossAnchor(PlayerbotAI* ai, Player* bot, BossAnchorCircle& out);
    // The same read, handing back the live anchor row itself (so a caller can take another role's circle of it).
    const BossTacticRow* LiveBossAnchorRow(PlayerbotAI* ai, Player* bot);
    // The pre-placed receiver's chase hold (DungeonRunSeekMeetPrePlaceHold, walk 59 F5, MeetPrePlaceHoldsChase): true
    // while the key is on, this bot is the live pre-place's receiver, its walk began, and `target` is the boss of its
    // live anchor row. Its reach actions (BossTacticsSeekHoldMultiplier) and its warrior gap closer then skip the boss.
    // The first hold of a pre-place logs one `event=preplace_hold` row naming `actionKind` (reach, charge, gapclose or intervene).
    bool SeekMeetPrePlaceHoldsChase(PlayerbotAI* ai, Player* bot, Unit* target, const char* actionKind = "reach");
    // Its receiver and liveness part alone (no target test): the key, the map filter and the one locked snapshot. The
    // multiplier holds the receiver's Intervene (a friendly target) on it; the first hold logs the row too.
    bool SeekMeetPrePlaceReceiverHeld(PlayerbotAI* ai, Player* bot, const char* actionKind);
    // Every boss/raid mover's destination goes through here before its MoveTo: unchanged without a live anchor or
    // inside it; outside, moved onto the circle toward the destination with the floor read there. False: the clamped
    // point has no floor on the centre's level, so the caller skips the move this tick.
    bool ClampMoveToBossAnchor(PlayerbotAI* ai, Player* bot, float& x, float& y, float& z);
    // Whether a live anchor keeps the bot from chasing this unit: the unit stands outside the bot's radius. The bot may
    // still taunt or shoot it from where it stands.
    // `raidCircle`: judge against the everyone circle (the raid's 30 yd) instead of the bot's own (a tank's 25 yd): the
    // off-tank's take of a Son on a non-tank, which stands where the raid stands.
    bool BossAnchorRefusesChase(PlayerbotAI* ai, Player* bot, Unit* unit, bool raidCircle = false);
    // The meet receiver's bounded chase exemption (DungeonRunSeekMeetTakeReach, SeekMeetReceiverExempt): true only when
    // `target` is the open meet's current Son, this bot is its receiver, and every bound holds (the Son alive and at
    // most 12 yd past the raid circle, the receiver not the boss's victim, the take due by the reach). The anchor's
    // chase refusal (BossAnchorRefusesChase) and ChaseTo's anchor gate and clamps skip that one target only.
    bool MeetSonChaseExempt(PlayerbotAI* ai, Player* bot, Unit* target);
    // A boss's anchor row from the table alone (no fight read): the run leader's stand search for a boss step reads
    // it before the pull. False when the boss has no anchor row.
    bool BossAnchorOfBoss(std::uint32_t map, std::uint32_t bossEntry, bool tank, BossAnchorCircle& out);

    // The spacing the ranged raid must keep right now: a live spread row's `param` (never below `fallback`), else
    // `fallback`. BossSpreadLive says whether any live spread row widened it (the stay exemption drops then).
    float BossSpreadYd(PlayerbotAI* ai, Player* bot, float fallback);
    bool BossSpreadLive(PlayerbotAI* ai, Player* bot);

    // A ranged bot's fixed point between the doses (DungeonRunBossSpreadSlots): the slot it owns in the ring order
    // around the boss anchor, the verdict that decides what it does with it, and the point itself. `slotBot` is false
    // for every bot the row does not apply to. The read never reads the key for its decision (the caller does); the key
    // only owns the row it writes, through SpreadLogOwned.
    struct BossSpreadSlotTask
    {
        bool slotBot = false;
        SpreadSlotVerdict verdict = SpreadSlotVerdict::NoCentre;
        int slot = -1;
        // The spare point the walk goes to when the owned slot is unusable; the row still reports `slot` and `slotYd`.
        int fallback = -1;
        // The centre the ring order was built around (`anchor`, `spot`, `none`); the row reports it.
        SpreadCentreKind centreKind = SpreadCentreKind::None;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float slotYd = -1.0f;
    };
    BossSpreadSlotTask FindBossSpreadSlot(PlayerbotAI* ai, Player* bot);
    // One `BossTacticsSpread` row per bot and 5 s: the key, the verdict word, the owned slot, the distance to it, the
    // fallback point actually used (-1 when none), the centre (`anchor`, `spot` or `none`), the nearest living group
    // member, how many members stand inside the row's spacing, the seek aura and the clock. A null `centre` derives it
    // from the live anchor and the seek spot (the legacy writer); `replaceSameTick` lets a failed move correct the row
    // its own tick's read wrote instead of being swallowed by it.
    void LogBossSpread(PlayerbotAI* ai, Player* bot, const char* verdict, int slot, float slotYd, int fallback,
                       const char* centre = nullptr, bool replaceSameTick = false);

    // The source a free DPS bot damages or burns: the one the gather stands around, while the dose window is open and
    // the row's hold allows the damage (the pre-damage, or the burn once the gather is ready). Null otherwise.
    Unit* FindBossSeekKill(PlayerbotAI* ai, Player* bot);

    // The second off-tank's job: take a source that fights, pull one while the gather still needs it, and hold it at
    // the row's spot. `take` is the Attack-and-taunt, `pull` the walk-out-and-grab, `walkToSpot` the drag back to B,
    // and `holdSwing` the window-shut stop of the tank's own auto-attack on a Son already close to the hold percent.
    // With fewer than three tanks alive the off-tank (tanks[1]) takes the Son over and the raid's nearest hunter (else
    // a ranged damage dealer) pulls it with one ranged shot (`rangedPull`).
    struct SeekTankTask
    {
        bool valid = false;
        const BossTacticRow* row = nullptr;
        Unit* source = nullptr;
        bool take = false;
        bool pull = false;
        bool rangedPull = false; // the fallback's one shot, instead of the tank's walk-in and swing
        bool fromAnchor = false; // a live boss anchor: the shot is fired from (x, y, z) inside it, never chased
        bool walkout = false;     // the walk-out task (SeekWalkoutPolicy.h): out to shoot `source` once, then back
        bool walkoutShot = false; // the shot landed: walk back into the tanks' circle
        std::uint32_t walkoutPullCastMs = 0; // the walk-out focus's pull cast went out then (0: none this task)
        std::uint32_t walkoutGen = 0;        // the task's start generation: the puller's own writes match it (WalkoutTaskIs)
        // Milliseconds to the next Blood Siphon (signed; INT_MAX: unknown), for the shot's wait (WalkoutShotReady).
        int walkoutDueInMs = std::numeric_limits<int>::max();
        bool meet = false;        // this take is the pulled Son's meet (the log row's meet=1)
        const char* takeSrc = "other"; // the take path that produced it (the log row's src=, SeekTakeSrcWord)
        Unit* boss = nullptr;     // the fight's boss (the relief row's cap read)
        // The relief pick's clock (the tick's state.nowMs): the take's claim, confirm and rollback all use it, so a
        // task that executes a tick later confirms a claim it still held at the pick.
        std::uint32_t pickMs = 0;
        bool prePlaceHold = false; // the pre-placed tank stands at its point (x, y): stop a walk, leave the rotation free
        bool walkToSpot = false;
        bool holdSwing = false;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    SeekTankTask FindBossSeekTank(PlayerbotAI* ai, Player* bot);

    // The seek walk: into the cloud, or to the gather around the dying source, and then stand.
    class BossTacticsSeekAction : public MovementAction
    {
    public:
        BossTacticsSeekAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics seek") {}
        bool isUseful() override;
        bool isPossible() override;
        bool Execute(Event& event) override;

    private:
        // The walk was started for this spot: MoveTo relaunches the spline on every call (the stand action's pattern).
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool hasLast = false;
    };

    // The fixed slot between the doses (DungeonRunBossSpreadSlots): stand on the owned point, walk back to it once the
    // seek lets go. A healer's cast is never cut for the walk; a damage dealer cuts its own after 6 s.
    class BossTacticsSpreadSlotAction : public MovementAction
    {
    public:
        BossTacticsSpreadSlotAction(PlayerbotAI* ai) : MovementAction(ai, "boss tactics spread slot") {}
        bool isUseful() override;
        bool isPossible() override;
        bool Execute(Event& event) override;

    private:
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool hasLast = false;
        // The first read of an unbroken run of `cast` verdicts; 0: the last verdict was another.
        std::uint32_t castSinceMs = 0;
        // When the `cast` verdict was last read: a read more than kSpreadSlotCastGapMs later starts the run over, so a
        // stamp left by another verdict or the previous pull cannot cut the first cast after a gather.
        std::uint32_t castSeenMs = 0;
    };

    // The second off-tank's source: take it, pull it, drag it to the row's spot.
    class BossTacticsSeekTankAction : public AttackAction
    {
    public:
        BossTacticsSeekTankAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics seek tank") {}
        bool Execute(Event& event) override;

    private:
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool hasLast = false;
    };

    // The free DPS bots' source damage: the pre-damage down to the hold percent, then the burn once the gather is
    // ready (the kill_first slot's priority; the two never both apply on one boss).
    class BossTacticsSeekKillAction : public AttackAction
    {
    public:
        BossTacticsSeekKillAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics seek kill") {}
        bool Execute(Event& event) override;
    };

    // While the bot seeks (walks into the cloud or to the gather), every other movement action is held so the return
    // to the stay, the raid spread, a reach, a follow or a hazard step-out cannot undo the walk.
    class BossTacticsSeekHoldMultiplier : public Multiplier
    {
    public:
        BossTacticsSeekHoldMultiplier(PlayerbotAI* ai) : Multiplier(ai, "boss tactics seek hold") {}
        float GetValue(Action* action) override;
    };
    // A taunt_after_cast row (mode until_tank) whose subject stopped being on a tank, for the one bot the row's raid
    // order names for the current window (`priority` and the roll of tanks that can taunt now): `valid` is true only
    // while that bot is the one asking and its window has not run out. `sinceMs` is how long ago the window opened,
    // `deadlineMs` its length (the row's param, 10 s when the row leaves it 0). A new window opens every deadline
    // while the subject stays off a tank. Only mode until_tank is read. A spell-less row opens on a player that is no tank;
    // a spell-keyed row (Jin'do's Banish 24466) also opens on the tank's teleport (TauntAfterVictim). A blink row, or a spell-keyed
    // row in any other mode, is accepted by the loader and does nothing here.
    struct BossTauntAfter
    {
        bool valid = false;
        Unit* subject = nullptr;
        const BossTacticRow* row = nullptr;
        int tankIndex = -1;          // the bot's slot in the raid tank order
        std::uint32_t deadlineMs = 0;
        std::uint32_t sinceMs = 0;   // since the current window opened
    };
    // One read of the taunt_after_cast rows of the fight: opens a window (the first, or the retry after one ran out),
    // logs a window that ran out or ended, and ends the episode when a tank has the subject again
    // (`BossTacticsTauntAfter`). Gated by AiPlayerbot.DungeonRunAuraTactics, AiPlayerbot.DungeonRunCharmYield and the
    // "boss tactics" strategy; the episode is one shared state per subject and row, advanced by the window's holder
    // alone (a single owner, so each transition and its row happen once), and the tank it names is the one that acts.
    BossTauntAfter FindBossTauntAfter(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);
    // The taunt of the window was cast: its outcome row then says taunted=1.
    void NoteBossTauntAfterCast(PlayerbotAI* ai, Player* bot, const BossTauntAfter& task);

    // The row's tank attacks the subject and casts its class taunt (`taunt`/`growl`) as soon as it can.
    class BossTacticsTauntAfterAction : public AttackAction
    {
    public:
        BossTacticsTauntAfterAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics taunt after") {}
        bool Execute(Event& event) override;
    };

    // One read of the tank_post rows of the fight: the post the bot's raid tank slot holds, the awaited unit it waits
    // for, and what the post does this tick. `valid` is true only for the row's own tank, only while the post runs and
    // only when the tank may act; `holdingOtherHostile` means the tank keeps another hostile that attacks a group
    // member, so the post waits and the action stands down (a hold_spot post clears it once the subject has arrived:
    // the subject then outranks that hostile). The post's state (start, arrival, window) is kept per bot
    // in one record, written and read here alone, so each post logs one start, one arrival and one end.
    struct BossTankPost
    {
        bool valid = false;
        bool holdingOtherHostile = false;
        TankPostDecision step = TankPostDecision::None;
        Unit* boss = nullptr;
        Unit* subject = nullptr;
        const BossTacticRow* row = nullptr;
        int tankIndex = -1;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool arrived = false;
    };
    // Gated by AiPlayerbot.DungeonRunTankPost, the map's tank_post rows and the "boss tactics" strategy.
    BossTankPost FindBossTankPost(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight);

    // The post's tank walks to the row's spot and holds there; on arrival it attacks the awaited unit. While walking or
    // holding it drops only the row's boss (an airborne boss) or nothing, and it never drops another hostile that
    // attacks a group member.
    class BossTacticsTankPostAction : public AttackAction
    {
    public:
        BossTacticsTankPostAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics tank post") {}
        bool Execute(Event& event) override;

    private:
        // The walk was started for this spot: MoveTo relaunches the spline on every call (the stand action's pattern).
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool hasLast = false;
    };

    // The ranged_only boss-tactic kind: this melee bot leaves `unit` alone while the unit stands the row's gap
    // (param, yards) or more above it (RangedOnlyPolicy.h). Gated by AiPlayerbot.DungeonRunRangedOnly, the map's rows
    // and the "boss tactics" strategy; ranged bots and healers are never held, and an explicit attack order wins.
    // `gapYdOut` (optional) the measured gap.
    bool IsRangedOnlyHeld(PlayerbotAI* ai, Player* bot, Unit* unit, float* gapYdOut = nullptr);
    // The `BossTacticsRangedOnly` row (`subject=<entry>, gap=<yd>`), at most once per bot per five seconds.
    void LogBossRangedOnly(PlayerbotAI* ai, Player* bot, Unit* subject, float gapYd);

    // The burn_mana row this bot burns for now, or null: the row's boss stands in the fight, alive, in combat and at
    // or above the row's start % of mana, the boss is not in the stone phase (aura 25685), and this bot is one of the
    // row's first `priority` burners that can cast now (ordered by guid; 0 = every eligible bot). `manaPctOut`
    // (optional) the boss's mana percent.
    Unit* FindBossBurnMana(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight, int* manaPctOut = nullptr);
    // Whether the bot may burn at all: alive, not a tank, not a healer (a shadow priest counts as damage), and it
    // knows mana burn, drain mana or viper sting.
    bool BossCanBurnMana(PlayerbotAI* ai, Player* bot);
    // The bot's 0-based place among the group's burners ordered by guid, or -1 when it cannot burn `boss` now. Only
    // members that run the boss tactics and can cast their own drain spell right now take a slot: alive, in combat,
    // in control (not stunned, feared, confused, charmed, silenced or pacified), the spell off cooldown, in its range
    // and with enough mana. The global cooldown and an in-flight cast do not count, so a busy burner keeps its slot.
    int BossBurnManaRank(PlayerbotAI* ai, Player* bot, Unit* boss);

    // The first castable drain spell the bot knows on the boss, or null (a hunter refreshes Viper Sting only when the
    // boss lacks it).
    const char* BossBurnManaSpell(PlayerbotAI* ai, Unit* boss);

    // Casts BossBurnManaSpell on the row's boss and writes the `BossTacticsBurnMana` row.
    class BossTacticsBurnManaAction : public AttackAction
    {
    public:
        BossTacticsBurnManaAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics burn mana") {}
        bool Execute(Event& event) override;
    };

    // Drops a subject this melee bot is held off (IsRangedOnlyHeld): the kill_first pick if there is one, else the
    // nearest attacker that is not held, else nothing (attack stopped so the assist rows re-pick once it lands).
    class BossTacticsRangedOnlyAction : public AttackAction
    {
    public:
        BossTacticsRangedOnlyAction(PlayerbotAI* ai) : AttackAction(ai, "boss tactics ranged only") {}
        bool Execute(Event& event) override;
    };
}
