"""Wiring pins for the avoid_cast boss tactic.

A creature in the fight casting a spell an `avoid_cast` row lists (ai_boss_tactic:
the caster's entry, the spell, the radius in `param`) sends every bot that is not
a tank and stands inside the radius out of it until the cast ends or the caster
dies. The rules live in ai/playerbot/BossTacticsPolicy.h (AvoidCastRadius, HoldsCaster,
MustLeaveCast, AvoidCastEscapes, CastHoldsMove, HasAvoidCastMap; their behaviour is
tested in tools/test_boss_tactics_policy.cpp). These pins only prove the live code
calls them:

 - the scan reads the caster's current generic spell and its channel (a channel
   runs in its own slot, CURRENT_CHANNELED_SPELL), through the policy's ListedCastRadius;
 - the trigger is registered, armed only for a bot whose tactics are on, and its
   action outranks the generic ground-effect step (avoid aoe, ACTION_EMERGENCY + 5);
 - while the cast runs, a move whose destination stands in a blast is held (on the
   combat and the non-combat engine), so a bot does not walk straight back in;
 - the loader's skip message names the new rule;
 - the walk out runs for a bot the run holds: on a stay (a pull's anchor hold,
   a healer's stand), in a pull's join window ("wait for attack"), and while
   the bot is casting itself (the reaction engine runs it; the combat engine
   sleeps through a cast, longer than the 3 s blast). The reaction does not
   break the cast beforehand: the walk-out breaks it once a walk is under way.

Run: python3 -m unittest tools/test_avoid_cast_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy"
ACTIONS = (STRATEGY / "actions/BossTacticsActions.cpp").read_text()
ACTIONS_H = (STRATEGY / "actions/BossTacticsActions.h").read_text()
TRIGGERS = (STRATEGY / "triggers/BossTacticsTriggers.cpp").read_text()
TRIGGERS_H = (STRATEGY / "triggers/BossTacticsTriggers.h").read_text()
TACTICS_STRATEGY = (STRATEGY / "generic/BossTacticsStrategy.cpp").read_text()
ACTION_CONTEXT = (STRATEGY / "actions/ActionContext.h").read_text()
TRIGGER_CONTEXT = (STRATEGY / "triggers/TriggerContext.h").read_text()
TACTICS_STRATEGY_H = (STRATEGY / "generic/BossTacticsStrategy.h").read_text()
COMBAT_STRATEGY = (STRATEGY / "generic/CombatStrategy.cpp").read_text()
MOVEMENT = (STRATEGY / "actions/MovementActions.cpp").read_text()
LOADER = (ROOT / "ai/playerbot/BossTacticsMgr.cpp").read_text()
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
DPS_TARGET = (STRATEGY / "values/DpsTargetValue.cpp").read_text()
TANK_TARGET = (STRATEGY / "values/TankTargetValue.cpp").read_text()
BLOCKERS = (STRATEGY / "values/HoldKillBlockersValue.cpp").read_text()
BLOCKERS_H = (STRATEGY / "values/HoldKillBlockersValue.h").read_text()
VALUE_CONTEXT = (STRATEGY / "values/ValueContext.h").read_text()
FOCUS = (STRATEGY / "generic/FocusStrategy.cpp").read_text()
AREA_SPELL_H = (STRATEGY / "actions/EnemyAreaSpell.h").read_text()
AOE_OFF = (ROOT / "ai/playerbot/policy/combat/AoeOffPolicy.h").read_text()
POLICY = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
POLICY_TEST = (ROOT / "tools/test_boss_tactics_policy.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


def node(text, trigger_name):
    match = re.search(r'"%s",\s*NextAction::array\(0,(.*?)\)\)\);' % re.escape(trigger_name), text, re.S)
    if not match:
        raise AssertionError("no TriggerNode for %s" % trigger_name)
    return match.group(1)


class AvoidCastWiring(unittest.TestCase):
    def test_scan_reads_the_generic_cast_and_the_policy(self):
        # Both cast slots as plain ids: a channel runs in its own slot, so a row that names a channelled blast
        # (the Zul'Gurub Mad Voidwalker's Consuming Shadows) is only seen when the channel is read too.
        ids = body(ACTIONS, "void CurrentCastIds(")
        # Each slot into its own variable, in this order: the ids cross the argument list below, so a swap here
        # would hide the channel behind the generic slot (or the reverse) while still looking right.
        self.assertIn("Spell* generic = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);", ids)
        self.assertIn("Spell* channeled = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);", ids)
        danger = body(ACTIONS, "float ai::CastDangerOf(")
        self.assertIn("CurrentCastIds(unit, genericId, channeledId)", danger)
        self.assertIn("ListedCastRadius(fight.rows, unit->GetEntry(), genericId, channeledId)", danger)
        fn = body(ACTIONS, "BossCastDanger ai::FindBossCastDanger(")
        self.assertIn("CastDangerOf(fight, unit)", fn)
        self.assertIn("MustLeaveCast(BotHoldsCaster(ai, bot, unit, fight.rows)", fn)

    def test_only_the_tank_holding_the_caster_stays(self):
        helper = body(ACTIONS, "bool BotHoldsCaster(")
        # The victim-to-player facts, the bot's own current target, and the caster's tank_hold row: all filled here.
        self.assertIn("const bool casterIsOnNonTank = victim && victim != bot", helper)
        self.assertIn("victim->GetTypeId() == TYPEID_PLAYER", helper)
        self.assertIn('AI_VALUE(Unit*, "current target") == caster', helper)
        self.assertIn("HasTankHold(active, caster->GetEntry())", helper)
        # The tank-target fallback (fix 1): a channelling caster's blast holds the AttackAction that would set
        # "current target", so a tank the loose-add choice picked up also counts through "tank target" — read only
        # behind the row check, so the reaction engine pays for the value on the one caster whose blast this tank
        # may hold, not every caster.
        self.assertIn('(casterTankHoldRow && PlayerbotAI::IsTank(bot) && AI_VALUE(Unit*, "tank target") == caster)',
                      helper)
        self.assertLess(helper.index("HasTankHold(active, caster->GetEntry())"), helper.index('"tank target"'))
        self.assertIn(
            "HoldsCaster(PlayerbotAI::IsTank(bot), casterIsOnBot, casterIsOnNonTank, botTargetsCaster, casterTankHoldRow)",
            helper)
        # The held casters' blasts are left out of the zones the hold and the walk-out read.
        zones = body(ACTIONS, "std::vector<CastDangerZone> ai::CastDangerZones(")
        self.assertIn("BotHoldsCaster(ai, bot, unit, fight.rows)", zones)

    def test_loose_add_prefers_a_tank_hold_caster_on_a_non_tank(self):
        # A tank holding nothing takes a caster a tank_hold row lists, that is on a non-tank, before the nearest
        # loose add (TankTargetValue.cpp): that is what makes it pick the Mad Voidwalker up at all.
        tank = (STRATEGY / "values/TankTargetValue.cpp").read_text()
        pickup = tank[tank.index("bool IsTankHoldPickup(PlayerbotAI* ai, Unit* unit)"):]
        pickup = pickup[:pickup.index("\n    }")]
        # Armed only with the boss tactics strategy in combat (fix 2): without it the loose-add order is exactly
        # the pre-row one (nearest first), and the row lookup is not even reached.
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', pickup)
        self.assertLess(pickup.index("HasStrategy"), pickup.index("GetBossTactics().Rows("))
        self.assertIn("GetBossTactics().Rows(unit->GetMapId(), unit->GetEntry())", pickup)
        self.assertIn("row.kind == BossTacticKind::TankHold", pickup)
        self.assertIn("dynamic_cast<Player*>(unit->GetVictim())", pickup)
        self.assertIn("!ai->IsTank(victimPlayer)", pickup)
        # The preference now runs above the main tank's stickiness (see
        # TankTakesTheLooseVoidwalker.test_the_pickup_check_precedes_the_main_tank_stickiness); the loose-add branch
        # below it keeps nearest first for everything else.
        loose = tank[tank.index("// Loose adds: nearest first"):]
        loose = loose[:loose.index("return bot->GetDistance(newUnit) < bot->GetDistance(oldUnit)")]
        self.assertNotIn("IsTankHoldPickup(", loose)

    def test_action_walks_to_a_policy_point_outside_every_cast(self):
        spots = body(ACTIONS, "int AvoidCastSpots(")
        self.assertIn("AvoidCastEscapes(", spots)
        self.assertIn("InsideAnyCastZone(zones,", spots)
        self.assertLess(spots.index("UpdateGroundPositionZ("), spots.index("InsideAnyCastZone("))
        self.assertIn("IsWithinLOS(", spots)
        fn = body(ACTIONS, "bool BossTacticsAvoidCastAction::Execute(")
        self.assertIn("AvoidCastSpots(bot, caster,", fn)
        self.assertIn("MoveTo(", fn)
        # Possible only with a usable way out, so the reaction engine does not pick it for a bot that has none.
        possible = body(ACTIONS, "bool BossTacticsAvoidCastAction::isPossible()")
        self.assertIn("AvoidCastSpots(bot, danger.caster,", possible)

    def test_trigger_is_armed_by_the_tactics(self):
        self.assertIn('Trigger(ai, "boss tactics avoid cast", 1)', TRIGGERS_H)
        fn = body(TRIGGERS, "bool BossTacticsAvoidCastTrigger::IsActive()")
        self.assertIn("Ready(ai, bot)", fn)
        self.assertIn("FindBossCastDanger(", fn)

    def test_registered_and_outranks_the_ground_step(self):
        self.assertIn('creators["boss tactics avoid cast"] = [](PlayerbotAI* ai) { return new BossTacticsAvoidCastTrigger(ai); };',
                      TRIGGER_CONTEXT)
        self.assertIn('creators["boss tactics avoid cast"] = [](PlayerbotAI* ai) { return new BossTacticsAvoidCastAction(ai); };',
                      ACTION_CONTEXT)
        self.assertIn('new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6)',
                      node(TACTICS_STRATEGY, "boss tactics avoid cast"))

    def test_reach_to_the_caster_is_held_during_the_cast(self):
        self.assertIn("class BossTacticsCastHoldMultiplier : public Multiplier", ACTIONS_H)
        fn = body(ACTIONS, "float BossTacticsCastHoldMultiplier::GetValue(")
        # Every movement action, the walk-out aside, by its target's position.
        self.assertIn("dynamic_cast<MovementAction*>(action)", fn)
        self.assertIn("dynamic_cast<BossTacticsAvoidCastAction*>(action)", fn)
        self.assertIn("CastHoldsMove(false, target == bot, true,", fn)
        # A follow's unit is its "follow target", not the action's target (the self target by default).
        self.assertIn("dynamic_cast<FollowAction*>(action)", fn)
        self.assertLess(fn.index("dynamic_cast<FollowAction*>(action)"), fn.index('AI_VALUE(Unit*, "follow target")'))
        self.assertIn('#include "FollowActions.h"', ACTIONS)
        # ...and the bot's formation spot around it, where the follow actually walks.
        self.assertIn('AI_VALUE(WorldPosition, "formation position")', fn)
        self.assertIn("CastHoldsMove(false, false, true, spot.getX(), spot.getY(), spot.getZ(), zones)", fn)
        # Only on a map with an avoid_cast row.
        self.assertLess(fn.index("HasAvoidCastMap(bot->GetMapId())"), fn.index("ScanBossTactics("))
        # On the combat and the non-combat engine.
        self.assertIn("multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));",
                      body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitCombatMultipliers("))
        self.assertIn("multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));",
                      body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitNonCombatMultipliers("))
        self.assertIn("void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;", TACTICS_STRATEGY_H)

    def test_the_non_combat_engine_runs_the_walk(self):
        fn = body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitNonCombatTriggers(")
        self.assertIn('new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6)', node(fn, "boss tactics avoid cast"))

    def test_loader_names_the_rule(self):
        self.assertIn("an avoid_cast without a spell or a radius", LOADER)


class AvoidCastIsLogged(unittest.TestCase):
    """ZG walk 7: a 3 s sampler of each bot's action never saw avoid_cast, so nothing proved it ran.

    The walk-out writes one bot_events.csv row per bot per cast (BossTacticsAvoidCast,
    caster=<entry>:<guid low>,spell=<id> / dist=,radius=,dest=,stopped=,walk=), through the same
    sPlayerbotAIConfig.logEvent the hunter telemetry uses, gated by bot_events.csv in
    AiPlayerbot.AllowedLogFiles.
    """

    def test_the_walk_out_emits_a_gated_row(self):
        fn = body(ACTIONS, "bool BossTacticsAvoidCastAction::Execute(")
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', fn)
        # The logged spell is the slot the radius came from, by the same pick (ListedCastSpell), not a second,
        # inline scan that can drift from it.
        self.assertIn("ListedCastSpell(fight.rows, caster)", fn)
        self.assertIn("LogAvoidCast(ai, bot, caster,", fn)
        # The distance before the move is read before the own cast is stopped and before MoveTo.
        self.assertLess(fn.index("distBefore ="), fn.index("CastStop()"))
        # The own cast is stopped only once a walk is under way (MoveTo can say yes without one): a bot with no way
        # out keeps its cast.
        self.assertLess(fn.index("MoveTo("), fn.index("WalkUnderWay(bot)"))
        self.assertLess(fn.index("WalkUnderWay(bot)"), fn.index("CastStop()"))
        self.assertIn("walk = AvoidCastWalk::Deferred;", fn)
        self.assertIn("return walk == AvoidCastWalk::Started;", fn)
        under = body(ACTIONS, "bool WalkUnderWay(")
        self.assertIn("!bot->movespline->Finalized()", under)
        # Every outcome logs, the walk that found no way out too.
        self.assertLess(fn.index("MoveTo("), fn.index("LogAvoidCast("))

    def test_the_row_format_and_the_event_name(self):
        fn = body(ACTIONS, "void LogAvoidCast(")
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsAvoidCast", info1, info2);', fn)
        self.assertIn('"caster=%u:%u,spell=%u"', fn)
        self.assertIn('"dist=%.1f,radius=%.1f,dest=%.1f,stopped=%d,walk=%s"', fn)
        self.assertIn('"dist=%.1f,radius=%.1f,dest=none,stopped=%d,walk=%s"', fn)
        self.assertIn("AvoidCastWalkName(walk)", fn)

    def test_one_row_per_bot_per_cast(self):
        fn = body(ACTIONS, "void LogAvoidCast(")
        self.assertIn("AvoidCastIsNewCast(g_avoidCastLog[bot->GetObjectGuid().GetRawValue()]", fn)
        # The throttle comes before any string is built.
        self.assertLess(fn.index("AvoidCastIsNewCast("), fn.index("snprintf("))
        # The cast instance (the caster's current Spell object) is part of the identity, not only a time gap.
        self.assertIn("reinterpret_cast<std::uintptr_t>(spell)", fn)
        policy = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
        self.assertIn("inline bool AvoidCastIsNewCast(", policy)


class AvoidCastRunsForHeldBots(unittest.TestCase):
    """ZG walk 6: 11 Bat Rider blasts, 5-12 bots in each, and not one walk out.

    The bots of a run sit on a stay (the pull's anchor hold, the healers'
    stands) inside a pull's join window, and the casters are mid-cast most of
    the fight. A MovementAction is useless on a stay (MovementAction::isUseful),
    the join window's multiplier zeroes every action it does not list, and the
    combat engine does not run while the bot casts.
    """

    def test_a_stay_does_not_make_the_walk_useless(self):
        # The base rule this has to get past.
        self.assertIn('return !ai->HasStrategy("stay", ai->GetState());', body(MOVEMENT, "bool MovementAction::isUseful()"))
        decl = body(ACTIONS_H, "class BossTacticsAvoidCastAction", "};")
        self.assertIn("bool isUseful() override;", decl)
        fn = body(ACTIONS, "bool BossTacticsAvoidCastAction::isUseful()")
        self.assertNotIn("MovementAction::isUseful", fn)
        self.assertNotIn('"stay"', fn)

    def test_the_join_window_lets_the_walk_through(self):
        fn = body(COMBAT_STRATEGY, "float WaitForAttackMultiplier::GetValue(")
        self.assertIn('(actionName != "boss tactics avoid cast")', fn)

    def test_the_reaction_engine_runs_the_walk(self):
        self.assertIn("void InitReactionTriggers(std::list<TriggerNode*>& triggers) override;", TACTICS_STRATEGY_H)
        fn = body(TACTICS_STRATEGY, "void BossTacticsStrategy::InitReactionTriggers(")
        self.assertIn('new NextAction("boss tactics avoid cast", ACTION_EMERGENCY + 6)', node(fn, "boss tactics avoid cast"))
        # The reaction engine would break the bot's cast and stop its walk before Execute (PlayerbotAI::UpdateAIReaction,
        # MovementAction's flags); the walk-out turns both off and does it itself once a walk is under way.
        decl = body(ACTIONS_H, "class BossTacticsAvoidCastAction", "};")
        self.assertIn("bool ShouldReactionInterruptCast() const override { return false; }", decl)
        self.assertIn("bool ShouldReactionInterruptMovement() const override { return false; }", decl)

    def test_the_reaction_fast_path_runs_for_armed_tactics(self):
        fn = body((STRATEGY / "ReactionEngine.cpp").read_text(), "bool ReactionEngine::HasReactionWork() const")
        # The map probe before the strategy lookup, and the group's attackers count as well as the bot's own
        # combat flag: a held bot out of combat itself still walks out.
        # Only on a map with an avoid_cast row, the map probe before the strategy lookup.
        self.assertLess(fn.index("GetBossTactics().HasAvoidCastMap(bot->GetMapId())"),
                        fn.index('ai->HasStrategy("boss tactics", BotState::BOT_STATE_REACTION)'))
        self.assertIn('GetValue<std::list<ObjectGuid>>("attackers")', fn)
        self.assertIn("TortoiseBots::BossTacticsReactionPass(inCombat, groupAttackers)", fn)

    def test_a_stay_does_not_walk_back_into_the_blast(self):
        fn = body(ACTIONS, "float BossTacticsCastHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<ReturnToStayPositionAction*>(action)", fn)
        self.assertIn("CastHoldsMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, zones)", fn)


class HoldKillWiring(unittest.TestCase):
    """ZG walk 21: four Mad Servants died inside 16 s and eight Mad Voidwalkers spawned in the raid at once, each
    shocking everyone within 15 yd of itself; 7 bots died in the next 2 s. The two `hold_kill` rows (budget 0: no DPS
    on a Mad Servant while a Mad Voidwalker or a Portal of Madness lives) pace the kills so a pair spawns at a time.

    The rules live in ai/playerbot/BossTacticsPolicy.h (KillHeld; its behaviour is tested in
    tools/test_boss_tactics_policy.cpp). These pins only prove the live code calls it, with a grid-search count.
    """

    def test_the_hold_multiplier_reads_killheld_for_dps(self):
        fn = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        # The DPS rows stand down on a held current target.
        self.assertIn("KillHeld(fight.rows, current->GetEntry(), countNear)", fn)
        self.assertIn('dynamic_cast<DpsAssistAction*>(action)', fn)
        self.assertIn('dynamic_cast<DpsAoeAction*>(action)', fn)
        # L-c: both hold clauses use the free-DPS gate HeldDpsTarget and the action use (IsFreeDps: no tank, no healer,
        # no explicit attack order), not a role test of their own.
        self.assertEqual(fn.count("freeDps"), 3)
        self.assertIn("const bool freeDps = IsFreeDps(ai, bot);", fn)
        self.assertIn("if (!freeDps)", fn)
        self.assertNotIn("PlayerbotAI::IsTank(bot)", fn)
        self.assertNotIn("PlayerbotAI::IsHeal(bot)", fn)
        # The AoE clause: a held creature in the pile the AoE would hit (one AoE killed four Servants together).
        self.assertIn("HeldUnitNear(current, fight, countNear)", fn)
        # L-a: the pick the row would send the bot to, not only the current target: the dps aoe target for the AoE rows,
        # the dps target for the assist rows.
        self.assertIn('aoe ? AI_VALUE(Unit*, "dps aoe target") : AI_VALUE(Unit*, "dps target")', fn)
        self.assertIn("KillHeld(fight.rows, picked->GetEntry(), countNear)", fn)

    def test_the_hold_kill_trigger_reads_killheld_and_dps_only(self):
        self.assertIn('Trigger(ai, "boss tactics hold kill", 1)', TRIGGERS_H)
        fn = body(TRIGGERS, "bool BossTacticsHoldKillTrigger::IsActive()")
        self.assertIn("Ready(ai, bot)", fn)
        self.assertLess(fn.index("ScanBossTactics("), fn.index("FindBossHoldKill("))
        helper = body(ACTIONS, "Unit* ai::FindBossHoldKill(")
        self.assertIn("IsFreeDps(ai, bot)", helper)                      # never a tank, a healer or an ordered bot
        self.assertIn('AI_VALUE(Unit*, "current target")', helper)
        self.assertIn("KillHeld(fight.rows, current->GetEntry(), countNear)", helper)

    def test_the_count_is_a_grid_search_not_the_attacker_list(self):
        # The Portal of Madness (15141) is unselectable and never attacks, so the blocker count can never come from the
        # bots' "attackers" list: the one count is a grid search in the "hold kill blockers" value.
        helper = body(BLOCKERS, "uint32 HoldKillBlockersValue::Calculate(")
        self.assertIn("bot->GetCreatureListWithEntryInGrid(found, entry, kHoldKillRangeYd)", helper)
        self.assertIn("creature->IsAlive()", helper)
        self.assertNotIn('"attackers"', helper)
        for fn in (body(ACTIONS, "std::uint32_t ai::HoldKillBlockers("), body(ACTIONS, "bool HeldUnitNear("),
                   body(ACTIONS, "Unit* ai::FindBossHoldKill("),
                   body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue("),
                   body(ACTIONS, "bool BossTacticsHoldKillAction::Execute("),
                   body(DPS_TARGET, "bool HeldDpsTarget(", "\n    }\n")):
            self.assertNotIn('"attackers"', fn)

    def test_the_grid_search_has_one_site_and_runs_once_a_second(self):
        # Cost: no grid search inside a candidate loop. The only call anywhere in the module is the value's, qualified by
        # the blocker entry and recalculated at most once a second; every reader goes through it.
        sites = [str(f.relative_to(ROOT)) for f in (ROOT / "ai").rglob("*")
                 if f.suffix in (".cpp", ".h") and f.read_text(errors="ignore").count("GetCreatureListWithEntryInGrid") > 0]
        self.assertEqual(sites, ["ai/playerbot/strategy/values/HoldKillBlockersValue.cpp"])
        self.assertEqual(BLOCKERS.count("GetCreatureListWithEntryInGrid"), 1)
        self.assertIn('CalculatedValue<uint32>(ai, "hold kill blockers", 1)', BLOCKERS_H)
        self.assertIn("public Qualified", BLOCKERS_H)
        self.assertIn(
            'creators["hold kill blockers"] = [](PlayerbotAI* ai) { return new HoldKillBlockersValue(ai); };',
            VALUE_CONTEXT)
        self.assertIn('#include "HoldKillBlockersValue.h"', VALUE_CONTEXT)
        reader = body(ACTIONS, "std::uint32_t ai::HoldKillBlockers(")
        self.assertIn('AI_VALUE2(uint32, "hold kill blockers", static_cast<int32>(entry))', reader)
        # Every reader builds its count from that one reader, none counts on its own.
        for text in (ACTIONS, DPS_TARGET):
            self.assertNotIn("CountNear", text)
        self.assertIn("HoldKillBlockers(ai, entry)", ACTIONS)
        # The readers added with one_at_a_time follow the same rule: the sole pick takes the fight's cached attacker
        # list, and nothing but HoldKillBlockersValue.cpp searches the grid.
        for name in ("HoldReason HeldSubjectNear(", "bool HeldUnitNear(",
                     "std::uint64_t ai::SoleKillGuid("):
            self.assertNotIn("GetCreatureListWithEntryInGrid", body(ACTIONS, name))
        self.assertEqual(DPS_TARGET.count("HoldKillBlockers(ai, entry)"), 1)

    def test_the_count_counts_only_fighting_or_unselectable_blockers(self):
        # A loose Voidwalker that has not woken yet (out of combat, selectable) does not hold the Servant; the portal,
        # which never fights, does: the filter is generic (no entry constants).
        helper = body(BLOCKERS, "uint32 HoldKillBlockersValue::Calculate(")
        self.assertIn("CountsAsBlocker(", helper)
        self.assertIn("creature->IsInCombat()", helper)
        self.assertIn("HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)", helper)
        policy = (ROOT / "ai/playerbot/BossTacticsPolicy.h").read_text()
        self.assertIn("inline bool CountsAsBlocker(", policy)

    def test_the_helper_is_defined_outside_the_anonymous_namespace(self):
        # A qualified definition (ai::Name) inside an anonymous namespace does not compile (the CountNear regression):
        # every `ai::` definition sits below the anonymous namespace's closing brace.
        close = ACTIONS.index("\n}\n\n// The blocker count of the hold_kill rows")
        self.assertLess(ACTIONS.index("namespace\n{"), close)
        for name in ("std::uint32_t ai::HoldKillBlockers(", "BossTacticsFight ai::ScanBossTactics("):
            self.assertGreater(ACTIONS.index(name), close)
        self.assertNotIn("ai::", ACTIONS[ACTIONS.index("namespace\n{"):close].replace("ai::PossibleAttack", ""))

    def test_the_action_switches_off_the_held_entry(self):
        fn = body(ACTIONS, "bool BossTacticsHoldKillAction::Execute(")
        self.assertIn("FindBossHoldKill(ai, bot, fight)", fn)
        # The kill_first pick first, else the nearest creature this bot may attack, by the kill_first candidate rules.
        self.assertIn("FindBossKillFirst(ai, bot, fight)", fn)
        self.assertIn("PossibleAttackTargetsValue::IsValid(unit, bot)", fn)
        self.assertIn("PickNearestUnheld(fight.rows, candidates, countNear)", fn)
        self.assertIn("Attack(bot, fight.units[nearestIndex])", fn)
        # Nothing to switch to: drop the held pick and stop attacking it, so the assist rows re-pick.
        self.assertIn('GetValue<Unit*>("current target")->Set(nullptr)', fn)
        self.assertIn('GetValue<ObjectGuid>("attack target")->Set(ObjectGuid())', fn)
        self.assertIn("bot->AttackStop()", fn)

    def test_registered_and_queued_beside_the_kill_first_pair(self):
        self.assertIn(
            'creators["boss tactics hold kill"] = [](PlayerbotAI* ai) { return new BossTacticsHoldKillTrigger(ai); };',
            TRIGGER_CONTEXT)
        self.assertIn(
            'creators["boss tactics hold kill"] = [](PlayerbotAI* ai) { return new BossTacticsHoldKillAction(ai); };',
            ACTION_CONTEXT)
        self.assertIn('new NextAction("boss tactics hold kill", ACTION_HIGH + 2)',
                      node(TACTICS_STRATEGY, "boss tactics hold kill"))


class HoldLivesInTheTargetChoice(unittest.TestCase):
    """H1/H2 (Codex H + cold M1): the hold-kill action fought the generic pickers (the dps target re-picked the held
    Servant the next tick) and the skip was only in the action. The pickers themselves drop a held entry now.

    The rules live in ai/playerbot/BossTacticsPolicy.h (DpsLeavesAlone, KillHeld) and are unit-tested in
    tools/test_boss_tactics_policy.cpp. These pins only prove the live code calls them.
    """

    def test_the_dps_and_aoe_pickers_skip_a_held_entry(self):
        fn = body(DPS_TARGET, "bool HeldDpsTarget(", "\n    }\n")
        # The same role gate the hold-kill action uses: a tank or a healer keeps its own pick, an explicit order wins.
        self.assertIn("PlayerbotAI::IsTank(bot)", fn)
        self.assertIn("PlayerbotAI::IsHeal(bot)", fn)
        self.assertIn('GetValue<ObjectGuid>("explicit attack target")', fn)
        # The rows of the candidate's own entry, and the blocker count: KillHeld on plain inputs.
        self.assertIn("table.Rows(bot->GetMapId(), unit->GetEntry())", fn)
        self.assertIn("DpsLeavesAlone(true, active, unit->GetEntry(), countNear)", fn)
        self.assertIn("HoldKillBlockers(ai, entry)", fn)
        # Every picker drops a held candidate: the three tournaments and the AoE pick, the group tank's victim and the
        # raid mark. With nothing unheld left the value returns null instead of the held pick.
        self.assertEqual(DPS_TARGET.count("HeldDpsTarget(ai, ai->GetBot(), attacker)"), 4)
        self.assertIn("HeldDpsTarget(ai, bot, tankTarget)", DPS_TARGET)
        self.assertIn("HeldDpsTarget(ai, bot, target) ? nullptr : target", DPS_TARGET)

    def test_the_hold_is_opt_in_and_checks_the_rows_first(self):
        # Opt-in like IsTankHoldPickup: a bot without the "boss tactics" strategy in combat never holds. Cheapest first:
        # the map, then the candidate's rows with a HoldKill row, then the strategy, then the role and order gates.
        fn = body(DPS_TARGET, "bool HeldDpsTarget(", "\n    }\n")
        self.assertIn('ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)
        order = ["table.HasMap(bot->GetMapId())", "table.Rows(bot->GetMapId(), unit->GetEntry())",
                 "BossTacticKind::HoldKill", 'ai->HasStrategy("boss tactics"', "PlayerbotAI::IsTank(bot)",
                 'GetValue<ObjectGuid>("explicit attack target")', "DpsLeavesAlone("]
        positions = [fn.index(token) for token in order]
        self.assertEqual(positions, sorted(positions))

    def test_the_aoe_mark_and_the_runner_finish_skip_a_held_entry(self):
        # L-a: DpsAoeTargetValue's raid-mark early return drops a held mark; L-b: the finish-runner pick skips a held one.
        fn = body(DPS_TARGET, "Unit* DpsAoeTargetValue::Calculate()")
        self.assertIn("rti && !AttackersValue::IsKeepoutSetAside(rti, bot) && !HeldDpsTarget(ai, bot, rti)", fn)
        dps = body(DPS_TARGET, "Unit* DpsTargetValue::Calculate()")
        self.assertIn("rti && !AttackersValue::IsKeepoutSetAside(rti, bot) && !HeldDpsTarget(ai, bot, rti)", dps)
        runner = DPS_TARGET[DPS_TARGET.index("IsFinishableRunner(facts)"):]
        runner = runner[:runner.index("chaser.ranged && !bot->IsWithinLOSInMap(unit)")]
        self.assertIn("IsWastedDpsTarget(ai, unit) || HeldDpsTarget(ai, bot, unit)", runner)

    def test_the_mark_yields_to_a_nearer_loose_voidwalker(self):
        # A TankHold row on the mark's own entry, and a tank_hold pickup nearer to this bot than the mark: the mark
        # drops and the tournament's loose bucket (pickups first) takes over. Without the row, as before.
        self.assertIn("MarkYieldsToNearerPickup(ai, bot, rti)", TANK_TARGET)
        fn = body(TANK_TARGET, "bool MarkYieldsToNearerPickup(")
        self.assertIn("MarkHasTankHoldRow(mark)", fn)
        self.assertIn("TankTakesMark(true, hasPickup, nearestPickup, bot->GetDistance(mark))", fn)
        self.assertIn("IsTankHoldPickup(ai, unit)", fn)

    def test_the_pickup_check_reads_the_strategy_gate(self):
        fn = body(TANK_TARGET, "bool IsTankHoldPickup(")
        self.assertIn('HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT)', fn)
        self.assertIn("BossTacticKind::TankHold", fn)
        self.assertIn("ai->IsTank(victimPlayer)", fn)


class OneAtATimeWiring(unittest.TestCase):
    """ZG walk 22 (F1): the hold_kill rows alone cannot stop the first burst. At most 3-4 Mad Servants of a wave are
    engaged, but they are engaged before the first dies, so `KillHeld` is false for everyone, the free DPS split over
    2-3 of them (dps tournament, the group tank's victim, AoE) and the first death lands 11-17 s in with the others at
    18-75 %.

    one_at_a_time (map 309, Mad Servant 15111) sends every DPS to one Servant: the pick is the lowest-health living
    unit of the entry that fights, the lowest guid on a tie, computed from the fight's cached attacker list on every
    bot, so they converge without a vote. The rules live in ai/playerbot/BossTacticsPolicy.h (PickSoleKill,
    OneAtATimeHolds) and are unit-tested in tools/test_boss_tactics_policy.cpp; these pins only prove the live code
    calls them.
    """

    def test_the_kind_the_loader_rule_and_the_shared_pick(self):
        self.assertIn("OneAtATime = 7,", POLICY)
        self.assertIn('if (text == "one_at_a_time")', POLICY)
        self.assertIn("return BossTacticKind::OneAtATime;", POLICY)
        # The row is about its own boss_entry (like a tank_hold row): a target_entry is refused.
        self.assertIn("if (row.kind == BossTacticKind::OneAtATime && row.targetEntry != 0)", POLICY)
        self.assertIn("struct SoleCandidate", POLICY)
        self.assertIn("inline int PickSoleKill(const std::vector<SoleCandidate>& c)", POLICY)
        self.assertIn("inline bool OneAtATimeHolds(std::uint64_t solePick, std::uint64_t guid)", POLICY)
        # Named and covered by the policy test.
        self.assertIn("a Mad Servant that is not the sole pick is held with no Voidwalker alive", POLICY_TEST)
        self.assertIn("PickSoleKill(", POLICY_TEST)
        self.assertIn("OneAtATimeHolds(", POLICY_TEST)

    def test_the_sole_pick_reads_the_cached_attacker_list_on_both_sides(self):
        # One pick rule, two readers: the dps target choice and the hold multiplier. Both hand the fight's cached
        # attacker list to ai::SoleKillGuid; neither runs a grid search of its own.
        helper = body(ACTIONS, "std::uint64_t ai::SoleKillGuid(")
        self.assertIn("candidate.guid = unit->GetObjectGuid().GetRawValue()", helper)
        self.assertIn("candidate.hpPct = static_cast<int>(unit->GetHealthPercent())", helper)
        self.assertIn("candidate.inCombat = unit->IsInCombat()", helper)
        self.assertIn("PickSoleKill(candidates)", helper)
        self.assertNotIn("GetCreatureListWithEntryInGrid", helper)
        paced = body(DPS_TARGET, "std::uint64_t PacedPick(", "\n    }\n")
        self.assertIn('GetValue<std::list<ObjectGuid>>("attackers")', paced)
        self.assertIn("SoleKillGuid(units, entry)", paced)
        fn = body(DPS_TARGET, "bool HeldDpsTarget(", "\n    }\n")
        self.assertIn("pacedRow", fn)
        self.assertIn("BossTacticKind::OneAtATime", fn)
        self.assertIn("OneAtATimeHolds(PacedPick(ai, unit->GetEntry()), unit->GetObjectGuid().GetRawValue())", fn)
        # An explicit attack order lifts the hold_kill exemption (the dps pickers keep it), never the pacing.
        self.assertLess(fn.index("OneAtATimeHolds(PacedPick("), fn.index("if (ordered)"))
        # The multiplier's held test reads the pick off the fight it already scanned, not off a second list.
        subject = body(ACTIONS, "HoldReason HeldSubjectNear(")
        self.assertIn("OneAtATimeHolds(SoleKillGuid(fight.units, row->bossEntry), "
                      "unit->GetObjectGuid().GetRawValue())", subject)


class HeldSubjectVetoWiring(unittest.TestCase):
    """ZG walk 22 (F1): what kept killing the held Mad Servants while the Voidwalkers were up was damage the hold never
    covered — class AoE and grenades on a Voidwalker standing inside the pile (40 grenades, 15 Rain of Fire, 8
    Multi-Shot in one fight), the DoTs the "attacker without aura" rows put on every other Servant (they tick on after
    the hold), and every focus-ordered bot, which the free-DPS gate exempts.
    """

    def test_the_area_clause_is_not_behind_the_free_dps_gate(self):
        fn = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        # Every non-tank, non-healer bot, an explicit order included: the gate is the role, not IsFreeDps.
        self.assertIn("IsBattleDps(bot)", fn)
        self.assertIn("IsAreaDamageAction(ai, action)", fn)
        self.assertIn("dynamic_cast<CastRangedDebuffSpellOnAttackerAction*>(action)", fn)
        # The area would land on the action's target, or on the bot itself (a self-centred blast, a sapper charge).
        self.assertIn("HeldSubjectNear(action->GetTarget(), fight, countNear, &subject)", fn)
        self.assertIn("HeldSubjectNear(bot, fight, countNear, &subject)", fn)
        # The clause sits above the free-DPS gates: the ordered bot never reaches them.
        self.assertLess(fn.index("HeldSubjectNear(action->GetTarget()"), fn.index("const bool freeDps = IsFreeDps(ai, bot);"))

    def test_the_area_detection_is_the_mark_the_name_set_and_the_spell(self):
        helper = body(ACTIONS, "bool IsAreaDamageAction(")
        self.assertIn("ActionThreatType::ACTION_THREAT_AOE", helper)
        self.assertIn("IsAreaSpellAction(action->GetName())", helper)
        self.assertIn('AI_VALUE2(uint32, "spell id", cast->GetName())', helper)
        self.assertIn("IsEnemyAreaSpell(sServerFacade.LookupSpellInfo(spellId))", helper)
        # The thrown consumables come from the shared name set: goblin sapper, throw grenade, oil of immolation.
        self.assertIn("inline bool IsAreaSpellAction(const std::string& actionName)", AOE_OFF)
        self.assertIn('"goblin sapper", "throw grenade"', AOE_OFF)

    def test_is_enemy_area_spell_is_shared_with_the_focus_veto(self):
        # The spell-shape test moved out of FocusStrategy.cpp's anonymous namespace: one predicate, two vetoes.
        self.assertIn("inline bool IsEnemyAreaSpell(SpellEntry const* spellInfo)", AREA_SPELL_H)
        self.assertIn("TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC", AREA_SPELL_H)
        self.assertIn("EffectChainTarget[i] > 1", AREA_SPELL_H)
        self.assertNotIn("bool IsEnemyAreaSpell(", FOCUS)
        self.assertIn('#include "playerbot/strategy/actions/EnemyAreaSpell.h"', FOCUS)
        self.assertIn("IsEnemyAreaSpell(sServerFacade.LookupSpellInfo(spellId))", FOCUS)
        self.assertIn('#include "EnemyAreaSpell.h"', ACTIONS)

    def test_the_held_test_is_one_helper_for_both_clauses(self):
        # The DoT row casts on "attacker without aura", which is not the bot's target: the veto reads the unit the cast
        # would land on through the action's own target value, and the held test is the one helper both clauses call.
        subject = body(ACTIONS, "HoldReason HeldSubjectNear(")
        self.assertIn("KillHeld(fight.rows, row->bossEntry, countNear)", subject)
        self.assertIn("kHoldKillAoeNearYd", subject)
        self.assertIn("return sole ? HoldReason::Sole : HoldReason::Budget;", subject)
        near = body(ACTIONS, "bool HeldUnitNear(")
        self.assertIn("HeldSubjectNear(around, fight, countNear, nullptr)", near)
        isa = body(ACTIONS, "bool IsBattleDps(")
        self.assertIn("!PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot)", isa)
        free = body(ACTIONS, "bool IsFreeDps(", "\n}\n")
        self.assertIn("if (!IsBattleDps(bot))", free)


class HoldKillIsLogged(unittest.TestCase):
    """F2 (ZG walk 22): the hold ran in walk 22 but nothing proved it. bot_events.csv had 6 800 rows and zero hold /
    kill-first / pickup rows, because the code logs none. One row per decision, rate-limited to one per bot per 5 s,
    gated by bot_events.csv in AiPlayerbot.AllowedLogFiles like the avoid_cast row.
    """

    def test_the_action_logs_its_pick(self):
        fn = body(ACTIONS, "bool BossTacticsHoldKillAction::Execute(")
        self.assertIn("LogHoldKillPick(ai, bot, fight, held,", fn)
        # Every exit that is a decision logs: the kill_first pick, the nearest unheld creature, the stop.
        self.assertEqual(fn.count("LogHoldKillPick("), 3)
        pick = body(ACTIONS, "void LogHoldKillPick(")
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsHoldKill", info1, info2);', pick)
        self.assertIn('"held=%u:%u,hp=%u"', pick)
        self.assertIn('"blockers="', pick)
        self.assertIn('info2 += "none";', pick)
        self.assertIn('info2 += ",to=" + std::to_string(destination->GetEntry())', pick)
        self.assertIn('info2 += ",to=stop";', pick)

    def test_every_multiplier_zero_logs_its_veto(self):
        fn = body(ACTIONS, "float BossTacticsHoldMultiplier::GetValue(")
        self.assertEqual(fn.count("LogHoldKillVeto("), 5)
        self.assertIn('const char* const veto = dpsAssist || tankAssist ? "assist" : aoe ? "aoe" '
                      ': attackerDebuff ? "dot" : "area";', fn)
        self.assertIn('held == HoldReason::Sole ? "sole" : veto', fn)
        veto = body(ACTIONS, "void LogHoldKillVeto(")
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsHoldKill", std::string("veto=") + veto, info2);', veto)
        self.assertIn('"held=%u:%u,hp=%u"', veto)
        self.assertIn('"held=none"', veto)

    def test_one_row_per_bot_per_five_seconds(self):
        due = body(ACTIONS, "bool HoldKillLogDue(")
        self.assertIn('hasLog("bot_events.csv")', due)
        self.assertIn("nowMs - last < kHoldKillLogGapMs", due)
        self.assertIn("constexpr uint32 kHoldKillLogGapMs = 5000;", ACTIONS)
        # The throttle comes before any string is built.
        for name in ("void LogHoldKillPick(", "void LogHoldKillVeto("):
            fn = body(ACTIONS, name)
            self.assertLess(fn.index("HoldKillLogDue("), fn.index("snprintf("))
            if name.startswith("void LogHoldKillPick"):
                self.assertLess(fn.index("HoldKillLogDue("), fn.index('std::string info2 = "blockers="'))


class TankTakesTheLooseVoidwalker(unittest.TestCase):
    """ZG fix 2: the run's kill_first focus put the skull on a Servant far out (both tanks targeted it while two
    Voidwalkers stood in the pile), and the explicit main tank never picked a loose add up at all.

    Failing case: "the explicit main tank takes a Voidwalker that is on a healer".
    """

    def test_commander_focus_skips_a_tank_keyed_to_tankhold(self):
        fn = body(COMMANDER, "static bool CommanderFocus(")
        self.assertIn("ai->IsTank(bot)", fn)
        skip = body(COMMANDER, "static bool FocusIsTankHoldTarget(")
        self.assertIn("GetBossTactics().Rows(target->GetMapId(), target->GetEntry())", skip)
        self.assertIn("BossTacticKind::TankHold", skip)
        self.assertIn("!target", skip)
        # The tank is un-stayed like everyone else (the run leader counts on every focus order doing that) but takes
        # neither the "rti" nor the "attack rti target" order, so its own pick keeps the loose adds.
        self.assertLess(fn.index("RelaxTacticalMovement(ai);"), fn.index("ai->IsTank(bot)"))
        self.assertLess(fn.index("ai->IsTank(bot)"), fn.index('Event("focus skull"'))
        self.assertLess(fn.index("ai->IsTank(bot)"), fn.index('Event("focus skull", "", order.lead)'))

    def test_the_pickup_check_precedes_the_main_tank_stickiness(self):
        # A tank_hold loose add outranks the explicit main tank's stickiness, or the main tank never leaves its Servant
        # for the Voidwalker on the healer.
        tank = (STRATEGY / "values/TankTargetValue.cpp").read_text()
        is_better = tank[tank.index("bool IsBetter(Unit* newUnit, Unit* oldUnit)"):]
        is_better = is_better[:is_better.index("\n    int GetIntervalLevel(")]
        self.assertIn("IsTankHoldPickup(ai, newUnit)", is_better)
        self.assertIn("IsTankHoldPickup(ai, oldUnit)", is_better)
        self.assertIn("if (newPickup != oldPickup)", is_better)
        self.assertLess(is_better.index("IsTankHoldPickup(ai, newUnit)"), is_better.index("MainTankSticksToCurrent("))
        self.assertLess(is_better.index("if (newPickup != oldPickup)"), is_better.index("MainTankSticksToCurrent("))


if __name__ == "__main__":
    unittest.main()
