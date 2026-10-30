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
 - the loader's skip message names the new rule.

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


if __name__ == "__main__":
    unittest.main()
