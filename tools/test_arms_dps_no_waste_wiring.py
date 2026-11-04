"""Arms DPS waste wiring (ZG underperformer fix 6).

An Arms warrior fighting as DPS must not Thunder Clap (grouped PvE) and must
hold Battle Stance mid-fight:

- Thunder Clap refuses grouped PvE Arms at the action gate; tanks, Fury,
  solo and PvP Arms keep it. Protection keeps its clap rows.
- The Battle Stance combat row swaps back after Pummel/Intercept moved the
  bot to Berserker, but never dances otherwise. Whirlwind fires only when
  already in Berserker, Sweeping Strikes only in Battle Stance on 3+
  targets, and both refuse tanks.
- The multiplier that holds spenders while Sweeping Strikes is ready uses
  the same "melee medium aoe" + Battle Stance gate as the action, so on 2
  targets neither fires and Arms keeps its single-target cycle.

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


def top_level_class_body(text, start):
    i = text.index(start)
    end = text.index("\n};", i)
    return text[i:end]


def rows(body):
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in ROW.finditer(body)]


def prereqs(text):
    return [(m.group(1), m.group(2), m.group(3)) for m in NODE_PREREQ.finditer(text)]


class ArmsDpsNoWasteWiringTest(unittest.TestCase):
    def test_thunder_clap_refuses_grouped_pve_arms(self):
        actions = ACTIONS.read_text()
        for cls in ("class CastThunderClapAction", "class CastThunderClapSnareAction"):
            body = class_body(actions, cls)
            self.assertIn("IsTank(bot)", body)
            self.assertIn('"arms"', body)
            self.assertIn('"pvp"', body)
            self.assertIn("HasActivePlayerMaster", body)

    def test_protection_keeps_thunder_clap(self):
        combat = rows(method_body(PROT, "ProtectionWarriorStrategy", "InitCombatTriggers"))
        self.assertTrue(any(action == "thunder clap" for _, action, _ in combat),
                        "tank clap row must stay")

    def test_arms_returns_to_battle_stance_in_combat(self):
        combat = rows(method_body(ARMS, "ArmsWarriorBuffStrategy", "InitCombatTriggers"))
        self.assertIn(("battle stance", "battle stance", "ACTION_NORMAL"), combat)
        self.assertFalse(any(trigger == "battle stance for arms" for trigger, _, _ in combat),
                         "pin trigger must be gone")
        # The plain row must not refuse a bot standing in Berserker (it swaps
        # back after Pummel/Intercept), and no pin action/trigger may remain.
        actions = ACTIONS.read_text()
        self.assertNotIn("CastBattleStanceForArmsPinAction", actions)
        self.assertNotIn("BattleStanceForArms", (WARRIOR / "WarriorTriggers.h").read_text())
        self.assertNotIn("BattleStanceForArmsTrigger::IsActive",
                         (WARRIOR / "WarriorTriggers.cpp").read_text())
        self.assertNotIn("battle stance for arms", CONTEXT.read_text())

    def test_multiplier_and_action_share_the_pack_gate(self):
        arms = ARMS.read_text()
        action = class_body(ACTIONS.read_text(), "class CastSweepingStrikesAction")
        for name in ("WarriorSweepingStrikesPveMultiplier",
                     "WarriorSweepingStrikesRaidMultiplier",
                     "WarriorSweepingStrikesPvpMultiplier"):
            mult = top_level_class_body(arms, "class %s" % name)
            self.assertIn("melee medium aoe", mult)
            self.assertIn('HasAura("battle stance"', mult)
            self.assertEqual(mult.count("melee light aoe"), 0,
                             "%s must not use the light gate" % name)
        self.assertIn("melee medium aoe", action)
        self.assertIn('HasAura("battle stance"', action)

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
