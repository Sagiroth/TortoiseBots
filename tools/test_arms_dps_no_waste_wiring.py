"""Arms DPS waste wiring (ZG underperformer fix 6).

An Arms warrior fighting as DPS must not Thunder Clap and must not swap
stances mid-fight:

- No "thunder clap" row in the Arms combat or Arms AoE combat path.
  Tanks are untouched (Protection keeps its clap rows).
- No in-combat stance-swap rows for Arms DPS: the Battle Stance buff pin
  only holds while not in a stance, and the Whirlwind / Sweeping Strikes
  nodes no longer dance. Whirlwind fires only when already in Berserker
  (its prereq action refuses tanks), Sweeping Strikes fires only in
  Battle Stance on 3+ targets (its action refuses tanks).

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
    def test_arms_combat_has_no_thunder_clap(self):
        combat = rows(method_body(ARMS, "ArmsWarriorStrategy", "InitCombatTriggers"))
        aoe = rows(method_body(ARMS, "ArmsWarriorAoeStrategy", "InitCombatTriggers"))
        for trigger, action, _ in combat + aoe:
            self.assertNotIn(action, ("thunder clap", "thunder clap on snare target"),
                             "trigger %s" % trigger)

    def test_protection_keeps_thunder_clap(self):
        combat = rows(method_body(PROT, "ProtectionWarriorStrategy", "InitCombatTriggers"))
        self.assertTrue(any(action == "thunder clap" for _, action, _ in combat),
                        "tank clap row must stay")

    def test_arms_battle_pin_stands_down_in_stance(self):
        body = class_body(ACTIONS.read_text(), "class CastBattleStanceForArmsAction")
        self.assertIn("IsTank(bot)", body)
        self.assertIn("berserker stance", body)
        self.assertIn("defensive stance", body)

    def test_whirlwind_dps_only_in_berserker_stance(self):
        arms = ARMS.read_text()
        self.assertIn('ACTION_NODE_P(whirlwind, "whirlwind", "berserker stance for whirlwind dps");',
                      arms)
        body = class_body(ACTIONS.read_text(), "class CastBerserkerStanceForWhirlwindDpsAction")
        self.assertIn("IsTank(bot)", body)
        self.assertIn('HasAura("berserker stance"', body)
        action = class_body(ACTIONS.read_text(), "class CastWhirlwindAction")
        self.assertIn("IsTank(bot)", action)
        self.assertIn('HasAura("berserker stance"', action)

    def test_sweeping_strikes_dps_only_in_battle_on_pack(self):
        self.assertIn('ACTION_NODE_P(sweeping strikes, "sweeping strikes", "battle stance for arms dps");',
                      ARMS.read_text())
        body = class_body(ACTIONS.read_text(), "class CastBattleStanceForArmsDpsAction")
        self.assertIn("IsTank(bot)", body)
        self.assertIn("melee medium aoe", body)
        action = class_body(ACTIONS.read_text(), "class CastSweepingStrikesAction")
        self.assertIn("IsTank(bot)", action)
        self.assertIn("melee medium aoe", action)
        self.assertIn('HasAura("battle stance"', action)

    def test_dps_stance_actions_registered(self):
        ctx = CONTEXT.read_text()
        self.assertIn('"berserker stance for whirlwind dps"', ctx)
        self.assertIn('"battle stance for arms dps"', ctx)

    def test_old_dance_prereq_gone(self):
        for _, action, prereq in prereqs(ARMS.read_text()):
            if action in ("whirlwind", "sweeping strikes"):
                self.assertNotIn("berserker stance for whirlwind\"",
                                 '"%s"' % prereq if prereq == "berserker stance for whirlwind" else "")
                self.assertNotEqual(prereq, "battle stance",
                                    "%s still pins plain battle stance" % action)


if __name__ == "__main__":
    unittest.main()
