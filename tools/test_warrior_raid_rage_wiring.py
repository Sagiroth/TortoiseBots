"""Warrior raid rage wiring (walk-15 fix).

Piercing Howl was armed by any snare unit in spell range but cast on the
melee target, so a runner 25 yd out made the warrior Howl the tank's mob.
The reach gate sits in isPossible, not isUseful: the engine pushes the
node's alternatives (Hamstring, Arms: Mocking Blow) only after an
impossible or failed action, so a Howl held back by reach or the raid rule
- and a warrior without the talent - must fail as impossible to fall
through. Mortal Strike was a debuff trigger (one cast per 10 s
debuff, blocked by another warrior). Rend (10 rage) re-applied on every
trash target starved the 30-rage main strike. The raid Whirlwind
prerequisite danced into Berserker Stance for a single target, and the
Sweeping Strikes multipliers still matched the old prerequisite name.

The behaviour lives in WarriorRageGatePolicy.h / RaidSnarePolicy.h /
WarriorWwStancePolicy.h (pinned by the standalone policy tests); this pins
the action and trigger wiring.

Run: python3 -m unittest tools/test_warrior_raid_rage_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"


def class_body(text, start):
    i = text.index(start)
    end = text.index("\n    };", i)
    return text[i:end]


class PiercingHowlWiringTest(unittest.TestCase):
    def setUp(self):
        self.actions = (WARRIOR / "WarriorActions.h").read_text()

    def test_gate_lives_in_is_possible(self):
        self.assertNotIn("MELEE_DEBUFF_ACTION_R(CastPiercingHowlAction", self.actions)
        self.assertIn("class CastPiercingHowlAction : public CastMeleeDebuffSpellAction", self.actions)
        body = class_body(self.actions, "class CastPiercingHowlAction")
        possible = body.index("bool isPossible() override")
        gate = body.index("PiercingHowlAllowed(")
        # A false isPossible pushes the node's alternatives (Hamstring,
        # Arms: Mocking Blow) in the engine; a false isUseful pushes none,
        # so the gate must sit after the possibility check, not before.
        self.assertGreater(gate, possible)
        # The base judgement runs first: an unknown spell or an out-of-reach
        # target is already impossible and never reads the snare rule.
        base = body.index("CastMeleeDebuffSpellAction::isPossible()")
        self.assertLess(base, gate)
        self.assertIn('"snare target", "hamstring"', body[:gate])
        # Every gate input is read from the live unit, not only passed on.
        self.assertIn("GetDistance2d(bot, snare)", body)
        # "||" keeps TIMED_FLEEING_MOTION_TYPE from satisfying this alone.
        self.assertIn("GetCurrentMovementGeneratorType() == FLEEING_MOTION_TYPE ||", body)
        self.assertIn("isRaidGroup()", body)

    def test_usefulness_is_the_base_judgement(self):
        # No isUseful override: a known Howl held back by the reach or raid
        # rule must stay useful so the engine reaches isPossible and takes
        # the fallback instead of spending the tick on nothing.
        body = class_body(self.actions, "class CastPiercingHowlAction")
        self.assertNotIn("isUseful", body)

    def test_action_is_registered(self):
        ctx = (WARRIOR / "WarriorAiObjectContext.cpp").read_text()
        self.assertIn('creators["piercing howl"] = [](PlayerbotAI* ai) { return new CastPiercingHowlAction(ai); };', ctx)


class RendRageGateWiringTest(unittest.TestCase):
    def test_rend_gates_on_the_main_strike_pool(self):
        actions = (WARRIOR / "WarriorActions.h").read_text()
        body = class_body(actions, "class CastRendAction")
        self.assertIn("ShouldRendForDps(", body)
        self.assertIn('ai->HasSpell("mortal strike")', body)
        self.assertIn('ai->HasSpell("bloodthirst")', body)
        # Tanks keep the unconditional row (TankWarriorStrategy): the bypass
        # must stay or a tank's Razor Hill rotation loses its only Rend.
        self.assertIn("IsTank(bot)", body)


class MortalStrikeTriggerWiringTest(unittest.TestCase):
    def test_trigger_is_can_cast_not_debuff(self):
        triggers = (WARRIOR / "WarriorTriggers.h").read_text()
        self.assertNotIn("DEBUFF_TRIGGER(MortalStrikeDebuffTrigger", triggers)
        self.assertIn('SpellCanBeCastedTrigger(ai, "mortal strike")', triggers)

    def test_trigger_registered_under_same_name(self):
        ctx = (WARRIOR / "WarriorAiObjectContext.cpp").read_text()
        self.assertIn('creators["mortal strike"] = [](PlayerbotAI* ai) { return new MortalStrikeTrigger(ai); };', ctx)


class WhirlwindRaidDanceWiringTest(unittest.TestCase):
    def setUp(self):
        self.actions = (WARRIOR / "WarriorActions.h").read_text()
        self.arms = (WARRIOR / "ArmsWarriorStrategy.cpp").read_text()

    def test_prereq_passes_raid_and_melee_density(self):
        body = class_body(self.actions, "class CastBerserkerStanceForWhirlwindAction")
        self.assertIn('"melee light aoe"', body)
        self.assertIn("isRaidGroup", body)
        self.assertIn("ShouldWhirlwindStanceDance(", body)

    def test_sweeping_strikes_multipliers_name_the_new_prereq(self):
        self.assertEqual(self.arms.count('actionName == "berserker stance for whirlwind"'), 3)


if __name__ == "__main__":
    unittest.main()
