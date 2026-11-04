"""Arms DPS waste wiring (ZG underperformer fix 6).

An Arms warrior fighting as DPS must not Thunder Clap and must not swap
stances mid-fight:

- Thunder Clap refuses non-tanks at the action gate, so no DPS row can
  fire it. Protection keeps its clap rows.
- The Battle Stance combat pin only holds while in no stance at all, so
  it never swaps back mid-fight. Whirlwind fires only when already in
  Berserker, Sweeping Strikes only in Battle Stance on 3+ targets, and
  both refuse tanks. Neither node carries a stance prerequisite, so there
  is no dance.

Run: python3 -m unittest tools/test_arms_dps_no_waste_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"
ARMS = WARRIOR / "ArmsWarriorStrategy.cpp"
PROT = WARRIOR / "ProtectionWarriorStrategy.cpp"
ACTIONS = WARRIOR / "WarriorActions.h"
TRIGGERS = WARRIOR / "WarriorTriggers.h"
CONTEXT = WARRIOR / "WarriorAiObjectContext.cpp"

ROW = re.compile(
    r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)'
)
NODE_PREREQ = re.compile(r'ACTION_NODE_P\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"')


def method_body(path, cls, method):
    """Body of `void <cls>::<method>(...)` (brace-matched)."""
    text = path.read_text()
    start = text.index("void %s::%s" % (cls, method))
    open_brace = text.index("{", start)
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace:i + 1]
    raise AssertionError("unbalanced braces in %s::%s" % (cls, method))


def class_body(text, start):
    i = text.index(start)
    end = text.index("\n    };", i)
    return text[i:end]


def rows(body):
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in ROW.finditer(body)]


def prereqs(text):
    return [(m.group(1), m.group(2), m.group(3)) for m in NODE_PREREQ.finditer(text)]


class ArmsDpsNoWasteWiringTest(unittest.TestCase):
    def test_thunder_clap_refuses_non_tanks(self):
        actions = ACTIONS.read_text()
        for cls in ("class CastThunderClapAction", "class CastThunderClapSnareAction"):
            body = class_body(actions, cls)
            self.assertIn("IsTank(bot)", body)

    def test_protection_keeps_thunder_clap(self):
        combat = rows(method_body(PROT, "ProtectionWarriorStrategy", "InitCombatTriggers"))
        self.assertTrue(any(action == "thunder clap" for _, action, _ in combat),
                        "tank clap row must stay")

    def test_arms_battle_pin_stands_down_in_stance(self):
        pin = rows(method_body(ARMS, "ArmsWarriorBuffStrategy", "InitCombatTriggers"))
        self.assertIn(("battle stance for arms", "battle stance for arms", "ACTION_NORMAL"), pin)
        body = class_body(ACTIONS.read_text(), "class CastBattleStanceForArmsPinAction")
        self.assertIn("IsTank(bot)", body)
        self.assertIn("berserker stance", body)
        self.assertIn("defensive stance", body)
        trigger = (WARRIOR / "WarriorTriggers.cpp").read_text()
        i = trigger.index("BattleStanceForArmsTrigger::IsActive")
        tbody = trigger[i:trigger.index("\n}", i)]
        self.assertIn("IsTank(bot)", tbody)

    def test_whirlwind_dps_only_in_berserker_stance(self):
        action = class_body(ACTIONS.read_text(), "class CastWhirlwindAction")
        self.assertIn("IsTank(bot)", action)
        self.assertIn('HasAura("berserker stance"', action)

    def test_sweeping_strikes_dps_only_in_battle_on_pack(self):
        action = class_body(ACTIONS.read_text(), "class CastSweepingStrikesAction")
        self.assertIn("IsTank(bot)", action)
        self.assertIn("melee medium aoe", action)
        self.assertIn('HasAura("battle stance"', action)
        aoe = rows(method_body(ARMS, "ArmsWarriorAoeStrategy", "InitCombatTriggers"))
        self.assertIn(("melee medium aoe", "sweeping strikes", "ACTION_HIGH + 5"), aoe)

    def test_arms_nodes_carry_no_stance_prereq(self):
        for _, action, _ in prereqs(ARMS.read_text()):
            self.assertNotIn(action, ("whirlwind", "sweeping strikes"),
                             "%s must not dance through a stance node" % action)


if __name__ == "__main__":
    unittest.main()
