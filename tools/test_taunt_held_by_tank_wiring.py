"""Taunt-off-tank wiring: generic taunts stand down at cast time.

Run: python3 -m unittest tools/test_taunt_held_by_tank_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"
PALADIN = ROOT / "ai/playerbot/strategy/paladin"
DRUID = ROOT / "ai/playerbot/strategy/druid"
SHAMAN = ROOT / "ai/playerbot/strategy/shaman"
ACTIONS = ROOT / "ai/playerbot/strategy/actions"
IS_USEFUL_CALL = "TauntTargetHeldByOtherTank(bot, GetTarget())"
IS_USEFUL_CALL_RE = re.compile(r"&&\s*!TauntTargetHeldByOtherTank\s*\(\s*bot\s*,\s*GetTarget\s*\(\s*\)\s*\)")
LOOSE_ADDS_CHECK_RE = re.compile(r'AI_VALUE2\s*\(\s*bool\s*,\s*"trigger active"\s*,\s*"loose adds near"\s*\)')


def class_body(text, name):
    """Body of `class <name>` up to its own closing `};`."""
    start = text.index("class " + name)
    rest = text[start:]
    end = re.search(r"\n\s*\};", rest)
    return rest[:end.end()] if end else rest


class TauntHeldByTankWiringTest(unittest.TestCase):
    def test_taunt_actions_stand_down_for_tank_victim(self):
        cases = [
            (WARRIOR / "WarriorActions.h", "CastMockingBlowAction"),
            (WARRIOR / "WarriorActions.h", "CastTauntAction"),
            (WARRIOR / "WarriorActions.h", "CastTauntOnSnareTargetAction"),
            (WARRIOR / "WarriorActions.h", "CastTauntOnAttackerAction"),
            (PALADIN / "PaladinActions.h", "CastHandOfReckoningAction"),
            (DRUID / "DruidActions.h", "CastGrowlAction"),
            (SHAMAN / "ShamanActions.h", "CastEarthshakerSlamAction"),
        ]
        for path, name in cases:
            with self.subTest(action=name):
                body = class_body(path.read_text(), name)
                self.assertIn("isUseful()", body)
                self.assertRegex(body, IS_USEFUL_CALL_RE)

    def test_on_attacker_actions_inherit_the_check(self):
        for path, child, parent in [
            (PALADIN / "PaladinActions.h", "CastHandOfReckoningOnAttackerAction",
             "CastHandOfReckoningAction"),
            (DRUID / "DruidActions.h", "CastGrowlOnAttackerAction", "CastGrowlAction"),
            (SHAMAN / "ShamanActions.h", "CastEarthshakerSlamOnAttackerAction",
             "CastEarthshakerSlamAction"),
        ]:
            with self.subTest(action=child):
                body = class_body(path.read_text(), child)
                self.assertIn("public " + parent, body)
                self.assertNotIn("isUseful", body)

    def test_aoe_taunts_rerun_the_loose_adds_count(self):
        for path, name in [
            (WARRIOR / "WarriorActions.h", "CastChallengingShoutAction"),
            (DRUID / "DruidActions.h", "CastChallengingRoarAction"),
        ]:
            with self.subTest(action=name):
                body = class_body(path.read_text(), name)
                self.assertIn("isUseful()", body)
                self.assertRegex(body, LOOSE_ADDS_CHECK_RE)


    def test_taunt_gate_does_not_stand_down(self):
        text = (ACTIONS / "BossTacticsActions.cpp").read_text()
        start = text.index("bool ai::TauntRefusedByGate(")
        end = text.index("\n}\n", start)
        self.assertNotIn("TauntTargetHeldByOtherTank", text[start:end])

    def test_boss_tactics_taunts_cast_directly(self):
        text = (ACTIONS / "BossTacticsActions.cpp").read_text()
        self.assertIn("ai->CastSpell(taunt, task.boss)", text)
        self.assertIn("ai->CastSpell(taunt, task.source)", text)
        self.assertIn("ai->CastSpell(taunt, task.subject)", text)
        self.assertEqual(text.count("TauntTargetHeldByOtherTank"), 1)

    def test_helper_uses_policy(self):
        text = (ACTIONS / "BossTacticsActions.cpp").read_text()
        start = text.index("bool ai::TauntTargetHeldByOtherTank(")
        end = text.index("\n}\n", start)
        body = text[start:end]
        self.assertIn("TauntStandsDownForTank(", body)
        self.assertIn("PlayerbotAI::IsTank(", body)

    def test_helper_passes_victim_facts_in_order(self):
        text = (ACTIONS / "BossTacticsActions.cpp").read_text()
        start = text.index("bool ai::TauntTargetHeldByOtherTank(")
        end = text.index("\n}\n", start)
        body = text[start:end]
        call = body[body.index("TauntHeldInputs in;"):]
        self.assertLess(call.index("in.victimIsPlayer"), call.index("in.victimIsSelf"))
        self.assertLess(call.index("in.victimIsSelf"), call.index("in.victimIsTank"))
        self.assertIn("return TauntStandsDownForTank(in);", body)


if __name__ == "__main__":
    unittest.main()
