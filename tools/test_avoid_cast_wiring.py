"""Wiring pins for the avoid_cast boss tactic.

A creature in the fight casting a spell an `avoid_cast` row lists (ai_boss_tactic:
the caster's entry, the spell, the radius in `param`) sends every bot that is not
a tank and stands inside the radius out of it until the cast ends or the caster
dies. The rules live in ai/playerbot/BossTacticsPolicy.h (AvoidCastRadius,
MustLeaveCast, AvoidCastDestinations; tested by tools/test_boss_tactics_policy.cpp).
These pins prove the live code calls them:

 - the scan reads the caster's current generic spell (a cast with a cast time),
   the same slot the interrupt scan reads;
 - the trigger is registered, armed only for a bot whose tactics are on, and its
   action outranks the generic ground-effect step (avoid aoe, ACTION_EMERGENCY + 5);
 - while the cast runs, a reach towards the caster is held, so a melee bot does
   not walk straight back in; the hold ends with the cast and the reach resumes;
 - the loader's skip message names the new rule;
 - the walk out runs for a bot the run holds: on a stay (a pull's anchor hold,
   a healer's stand), in a pull's join window ("wait for attack"), and while
   the bot is casting itself (the reaction engine runs it and breaks the cast;
   the combat engine sleeps through a cast, longer than the 3 s blast); a stay
   does not walk the bot back into the blast while the cast runs.

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
        danger = body(ACTIONS, "float ai::CastDangerOf(")
        self.assertIn("GetCurrentSpell(CURRENT_GENERIC_SPELL)", danger)
        self.assertIn("AvoidCastRadius(fight.rows", danger)
        fn = body(ACTIONS, "BossCastDanger ai::FindBossCastDanger(")
        self.assertIn("CastDangerOf(fight, unit)", fn)
        self.assertIn("MustLeaveCast(HoldsCaster(bot, unit)", fn)

    def test_only_the_tank_holding_the_caster_stays(self):
        helper = body(ACTIONS, "bool HoldsCaster(")
        self.assertIn("PlayerbotAI::IsTank(bot)", helper)
        self.assertIn("caster->GetVictim() == bot", helper)
        hold = body(ACTIONS, "float BossTacticsCastHoldMultiplier::GetValue(")
        self.assertIn("HoldsCaster(bot, target)", hold)

    def test_action_walks_to_a_policy_point_outside_every_cast(self):
        fn = body(ACTIONS, "bool BossTacticsAvoidCastAction::Execute(")
        self.assertIn("AvoidCastDestinations(", fn)
        self.assertIn("InsideCastDanger(", fn)
        self.assertIn("MoveTo(", fn)

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
        self.assertIn("dynamic_cast<ReachTargetAction*>(action)", fn)
        self.assertIn("CastDangerOf(", fn)
        self.assertIn("multipliers.push_back(new BossTacticsCastHoldMultiplier(ai));", TACTICS_STRATEGY)

    def test_loader_names_the_rule(self):
        self.assertIn("an avoid_cast without a spell or a radius", LOADER)


class AvoidCastIsLogged(unittest.TestCase):
    """ZG walk 7: a 3 s sampler of each bot's action never saw avoid_cast, so nothing proved it ran.

    The walk-out writes one bot_events.csv row per bot per cast (BossTacticsAvoidCast,
    caster=<entry>:<guid low>,spell=<id> / dist=,radius=,dest=,stopped=), through the same
    sPlayerbotAIConfig.logEvent the hunter telemetry uses, gated by bot_events.csv in
    AiPlayerbot.AllowedLogFiles.
    """

    def test_the_walk_out_emits_a_gated_row(self):
        fn = body(ACTIONS, "bool BossTacticsAvoidCastAction::Execute(")
        self.assertIn('sPlayerbotAIConfig.hasLog("bot_events.csv")', fn)
        self.assertIn("LogAvoidCast(ai, bot, caster,", fn)
        # The distance before the move is read before the own cast is stopped and before MoveTo.
        self.assertLess(fn.index("distBefore ="), fn.index("CastStop()"))
        self.assertLess(fn.index("CastStop()"), fn.index("MoveTo("))
        # The own cast is stopped only for a point that passed the blast and sight checks: a bot with no way out
        # keeps its cast (Codex adversarial review).
        self.assertLess(fn.index("InsideCastDanger(fight, points[i]"), fn.index("CastStop()"))
        self.assertLess(fn.index("IsWithinLOS("), fn.index("CastStop()"))
        # Every outcome logs, the walk that found no way out too.
        self.assertLess(fn.index("MoveTo("), fn.index("LogAvoidCast("))

    def test_the_row_format_and_the_event_name(self):
        fn = body(ACTIONS, "void LogAvoidCast(")
        self.assertIn('sPlayerbotAIConfig.logEvent(ai, "BossTacticsAvoidCast", info1, info2);', fn)
        self.assertIn('"caster=%u:%u,spell=%u"', fn)
        self.assertIn('"dist=%.1f,radius=%.1f,dest=%.1f,stopped=%d"', fn)
        self.assertIn('"dist=%.1f,radius=%.1f,dest=none,stopped=%d"', fn)

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
        # The reaction breaks the bot's own cast first (MovementAction's reaction flags).
        self.assertIn("bool ShouldReactionInterruptCast() const override { return true; }",
                      (STRATEGY / "actions/MovementActions.h").read_text())

    def test_the_reaction_fast_path_runs_for_armed_tactics(self):
        fn = body((STRATEGY / "ReactionEngine.cpp").read_text(), "bool ReactionEngine::HasReactionWork() const")
        # The map probe before the strategy lookup, and the group's attackers count as well as the bot's own
        # combat flag: a held bot out of combat itself still walks out (Codex adversarial review).
        self.assertLess(fn.index("GetBossTactics().HasMap(bot->GetMapId())"),
                        fn.index('ai->HasStrategy("boss tactics", BotState::BOT_STATE_REACTION)'))
        self.assertIn('GetValue<std::list<ObjectGuid>>("attackers")', fn)
        self.assertIn("TortoiseBots::BossTacticsReactionPass(inCombat, groupAttackers, true)", fn)

    def test_a_stay_does_not_walk_back_into_the_blast(self):
        fn = body(ACTIONS, "float BossTacticsCastHoldMultiplier::GetValue(")
        self.assertIn("dynamic_cast<ReturnToStayPositionAction*>(action)", fn)
        self.assertIn("InsideCastDanger(", fn)


if __name__ == "__main__":
    unittest.main()
