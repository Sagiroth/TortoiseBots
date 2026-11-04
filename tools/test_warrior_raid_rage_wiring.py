"""Warrior raid rage wiring (walk-15 fix).

Piercing Howl was armed by any snare unit in spell range but cast on the
melee target, so a runner 25 yd out made the warrior Howl the tank's mob.
The snare rule splits in two: a Howl refused in a raid is useless (the
engine pushes alternatives only after an impossible or failed action, and
the Arms and Fury alternative is a 30-yd Hamstring walk), while a refusal
in a dungeon stays impossible so the Hamstring fallback runs.
Mortal Strike was a debuff trigger (one cast per 10 s debuff, blocked by
another warrior). Rend (10 rage) re-applied on every trash target starved
the 30-rage main strike. The raid Whirlwind prerequisite danced into
Berserker Stance for a single target, and the Sweeping Strikes
multipliers still matched the old prerequisite name. Fury inherited the
Battle Stance out-of-combat row, costing a Battle/Berserker swap per
fight; Fury now rests in Berserker Stance and only the shared gear-stone
row comes from the base strategy.

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


def top_level_class_body(text, start):
    i = text.index(start)
    end = text.index("\n};", i)
    return text[i:end]


def function_body(text, signature):
    i = text.index(signature)
    end = text.index("\n}", i)
    return text[i:end]


class PiercingHowlWiringTest(unittest.TestCase):
    def setUp(self):
        self.actions = (WARRIOR / "WarriorActions.h").read_text()

    def test_gate_splits_useful_from_possible(self):
        self.assertNotIn("MELEE_DEBUFF_ACTION_R(CastPiercingHowlAction", self.actions)
        self.assertIn("class CastPiercingHowlAction : public CastMeleeDebuffSpellAction", self.actions)
        body = class_body(self.actions, "class CastPiercingHowlAction")
        useful = body.index("bool isUseful() override")
        possible = body.index("bool isPossible() override")
        # A raid refusal must be useless: the engine pushes the node's
        # alternatives (Hamstring) only after an impossible or failed
        # action. A dungeon refusal stays impossible so its Hamstring
        # fallback runs.
        self.assertIn("CastMeleeDebuffSpellAction::isUseful() && Gate() != HowlGate::Skip",
                      body[useful:possible])
        self.assertIn("CastMeleeDebuffSpellAction::isPossible() && Gate() == HowlGate::Cast",
                      body[possible:])
        # The snare unit is read once, in the helper both overrides share.
        gate = body.index("HowlGate Gate()")
        self.assertEqual(body.count('"snare target", "hamstring"'), 1)
        self.assertIn('"snare target", "hamstring"', body[gate:])
        self.assertIn("GetDistance2d(bot, snare)", body[gate:])
        # "||" keeps TIMED_FLEEING_MOTION_TYPE from satisfying this alone.
        self.assertIn("GetCurrentMovementGeneratorType() == FLEEING_MOTION_TYPE ||", body[gate:])
        self.assertIn("isRaidGroup()", body[gate:])

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

    def test_sweeping_strikes_multipliers_match_the_action_gate(self):
        for name in ("WarriorSweepingStrikesPveMultiplier",
                     "WarriorSweepingStrikesRaidMultiplier",
                     "WarriorSweepingStrikesPvpMultiplier"):
            mult = top_level_class_body(self.arms, "class %s" % name)
            self.assertIn('"melee medium aoe"', mult)
            self.assertIn('HasAura("battle stance"', mult)


class ArmsHowlFallbackWiringTest(unittest.TestCase):
    def test_howl_falls_back_to_hamstring_not_a_taunt(self):
        arms = (WARRIOR / "ArmsWarriorStrategy.cpp").read_text()
        self.assertIn('ACTION_NODE_A(piercing_howl, "piercing howl", "hamstring")', arms)
        self.assertNotIn('"piercing howl", "mocking blow"', arms)


class FuryRestStanceWiringTest(unittest.TestCase):
    def setUp(self):
        self.fury = (WARRIOR / "FuryWarriorStrategy.cpp").read_text()
        self.warrior = (WARRIOR / "WarriorStrategy.cpp").read_text()

    def test_fury_noncombat_rests_in_berserker(self):
        # The shared warrior non-combat row put every warrior in Battle
        # Stance, so a Fury warrior paid a Battle/Berserker swap per fight
        # (walk 16: Wink 14 + 14 over 11 fights). Fury rests in Berserker.
        body = function_body(self.fury,
            "void FuryWarriorBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)")
        self.assertNotIn("WarriorBuffStrategy::InitNonCombatTriggers(triggers)", body)
        self.assertIn("BuffStrategy::InitNonCombatTriggers(triggers)", body)
        self.assertIn("InitNonCombatSharedTriggers(triggers)", body)
        self.assertIn('"berserker stance"', body)

    def test_warrior_noncombat_keeps_battle_stance_and_shared_rows(self):
        # Arms and Protection keep the base row (battle stance plus the
        # gear stone); only the stance row moved out of the shared helper.
        body = function_body(self.warrior,
            "void WarriorBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)")
        self.assertIn('"battle stance"', body)
        self.assertIn("InitNonCombatSharedTriggers(triggers)", body)
        shared = function_body(self.warrior,
            "void WarriorBuffStrategy::InitNonCombatSharedTriggers(std::list<TriggerNode*>& triggers)")
        self.assertIn('"apply stone"', shared)


if __name__ == "__main__":
    unittest.main()
