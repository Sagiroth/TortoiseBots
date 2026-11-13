"""Protection warrior second-taunt and AoE-taunt wiring (T1: W2, W3).

Run: python3 -m unittest tools/test_warrior_tank_taunt_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"
TRIGGERS = ROOT / "ai/playerbot/strategy/triggers"


def rows(path):
    """(trigger, action, relevance expression) for every single-action row."""
    text = (WARRIOR / path).read_text()
    pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(text)]


class WarriorTankTauntWiringTest(unittest.TestCase):
    def test_mocking_blow_row(self):
        combat = (WARRIOR / "ProtectionWarriorStrategy.cpp").read_text()
        self.assertIn(("mocking blow taunt", "mocking blow", "ACTION_INTERRUPT"), rows("ProtectionWarriorStrategy.cpp"))
        self.assertIn('ACTION_NODE_P(mocking_blow, "mocking blow", "battle stance for mocking blow");', combat)

    def test_mocking_blow_creators(self):
        ctx = (WARRIOR / "WarriorAiObjectContext.cpp").read_text()
        self.assertIn('creators["mocking blow taunt"]', ctx)
        self.assertIn('creators["battle stance for mocking blow"]', ctx)

    def test_mocking_blow_trigger_and_action(self):
        triggers = (WARRIOR / "WarriorTriggers.h").read_text()
        self.assertIn("MockingBlowTauntTrigger", triggers)
        body_start = triggers.index("class MockingBlowTauntTrigger")
        body = triggers[body_start:body_start + 1500]
        self.assertIn("ShouldMockingBlow", body)
        self.assertIn("LoseAggroTrigger::IsActive()", body)
        actions = (WARRIOR / "WarriorActions.h").read_text()
        action_start = actions.index("CastBattleStanceForMockingBlowAction")
        action_body = actions[action_start:action_start + 1200]
        self.assertIn('"mocking blow taunt"', action_body)
        self.assertIn('"trigger active"', action_body)

    def test_loose_adds_row_replaces_pack_shout(self):
        aoe_rows = rows("ProtectionWarriorStrategy.cpp")
        aoe_text = (WARRIOR / "ProtectionWarriorStrategy.cpp").read_text()
        aoe_combat = aoe_text[aoe_text.index("ProtectionWarriorAoeStrategy::InitCombatTriggers"):]
        aoe_combat = aoe_combat[:aoe_combat.index("InitNonCombatTriggers")]
        self.assertIn(("loose adds near", "challenging shout", "ACTION_INTERRUPT + 2"),
                      [(t, a, r) for (t, a, r) in aoe_rows if a == "challenging shout"])
        self.assertNotIn(("melee medium aoe", "challenging shout", "ACTION_HIGH + 1"),
                         [(t, a, r) for (t, a, r) in aoe_rows if a == "challenging shout"])
        self.assertRegex(aoe_combat, r'"melee medium aoe",\s*NextAction::array\(0, new NextAction\("battle shout taunt"')

    def test_loose_adds_trigger_registered(self):
        self.assertIn('creators["loose adds near"]', (TRIGGERS / "TriggerContext.h").read_text())

    def test_loose_adds_sees_past_focus_marks(self):
        # Item 4: the shared walk counts the group's real attackers (core
        # GetAttackers per group member), not the "attackers" value that
        # "focus rti targets" collapses to the marked target.
        cpp = (ROOT / "ai/playerbot/strategy/actions/BossTacticsActions.cpp").read_text()
        start = cpp.index("bool ai::AoeTauntStillNeeded(")
        code = "\n".join(l for l in cpp[start:cpp.index("\n}\n", start)].splitlines()
                         if not l.strip().startswith("//"))
        self.assertIn("GetAttackers()", code)
        self.assertIn("IsPlayer()", code)
        self.assertNotIn('"attackers"', code)
        trig = (TRIGGERS / "GenericTriggers.cpp").read_text()
        fn = trig[trig.index("bool LooseAddsNearTrigger::IsActive()"):]
        fn = fn[:fn.index("\n}\n")]
        self.assertIn("AoeTauntStillNeeded(ai, bot)", fn)

if __name__ == "__main__":
    unittest.main()
