"""Warrior gap-closer wiring (AiPlayerbot.WarriorGapCloser).

The decision lives in WarriorGapCloserPolicy.h (tools/test_warrior_gap_closer_policy.cpp);
this file pins the rows and nodes that turn it into casts:

- Charge goes through "battle stance for charge", Intercept through
  "berserker stance for intercept" (the plain stance actions refuse tanks).
- The Protection rows outrank "taunt on attacker" (whose Defensive Stance
  prerequisite would swap back between the swap and the cast) and the tank's
  "defensive stance" row, which is what returns the tank to Defensive Stance
  once the gap closer is spent.
- Arms and Fury get the Charge row through WarriorStrategy; Protection, which
  skips the base combat rows, carries both rows itself.
- The config key's code default equals the shipped template.

Run: python3 -m unittest tools/test_warrior_gap_closer_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WARRIOR = ROOT / "ai/playerbot/strategy/warrior"
PROT = WARRIOR / "ProtectionWarriorStrategy.cpp"
BASE = WARRIOR / "WarriorStrategy.cpp"
CONTEXT = WARRIOR / "WarriorAiObjectContext.cpp"
CONFIG_CPP = ROOT / "ai/playerbot/PlayerbotAIConfig.cpp"
CONF_DIST = ROOT / "ai/playerbot/aiplayerbot.conf.dist.in"

ROW = re.compile(
    r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)'
)
NODE_PREREQ = re.compile(r'ACTION_NODE_P\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"')

LEVELS = {"ACTION_IDLE": 1, "ACTION_NORMAL": 10, "ACTION_HIGH": 20, "ACTION_MOVE": 30,
          "ACTION_INTERRUPT": 40, "ACTION_DISPEL": 50, "ACTION_LIGHT_HEAL": 60,
          "ACTION_MEDIUM_HEAL": 70, "ACTION_CRITICAL_HEAL": 80, "ACTION_EMERGENCY": 90}


def relevance(expr):
    expr = expr.strip()
    m = re.fullmatch(r"(ACTION_\w+)(?:\s*([+-])\s*(\d+))?", expr)
    if not m:
        raise AssertionError("unparsed relevance %r" % expr)
    value = LEVELS[m.group(1)]
    if m.group(2):
        value += int(m.group(3)) * (1 if m.group(2) == "+" else -1)
    return value


def method_body(path, cls, method):
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
    raise AssertionError("unbalanced body")


def rows(body):
    return {(t, a): relevance(r) for t, a, r in ROW.findall(body)}


class WarriorGapCloserWiringTest(unittest.TestCase):
    def test_stance_prerequisites(self):
        nodes = {action: pre for _, action, pre in NODE_PREREQ.findall(BASE.read_text())}
        self.assertEqual(nodes.get("gap closer charge"), "battle stance for charge")
        self.assertEqual(nodes.get("gap closer intercept"), "berserker stance for intercept")

    def test_protection_rows_outrank_the_stance_return(self):
        prot = rows(method_body(PROT, "ProtectionWarriorStrategy", "InitCombatTriggers"))
        charge = prot[("warrior gap closer charge", "gap closer charge")]
        intercept = prot[("warrior gap closer intercept", "gap closer intercept")]
        back = prot[("defensive stance", "defensive stance")]
        taunt = prot[("party member attacked", "taunt on attacker")]
        for closer in (charge, intercept):
            self.assertGreater(closer, back)
            self.assertGreater(closer, taunt)
            self.assertLess(closer, LEVELS["ACTION_DISPEL"])

    def test_dps_specs_get_the_charge_row(self):
        base = rows(method_body(BASE, "WarriorStrategy", "InitCombatTriggers"))
        charge = base[("warrior gap closer charge", "gap closer charge")]
        # Above Fury's Berserker Stance row (ACTION_MOVE) so it does not swap back first.
        self.assertGreater(charge, LEVELS["ACTION_MOVE"])
        self.assertNotIn(("warrior gap closer intercept", "gap closer intercept"), base)

    def test_creators_registered(self):
        text = CONTEXT.read_text()
        for name in ("warrior gap closer charge", "warrior gap closer intercept", "gap closer charge",
                     "gap closer intercept", "battle stance for charge", "berserker stance for intercept"):
            self.assertIn('creators["%s"]' % name, text)

    def test_config_default_matches_template(self):
        self.assertIn('config.GetBoolDefault("AiPlayerbot.WarriorGapCloser", false)', CONFIG_CPP.read_text())
        self.assertRegex(CONF_DIST.read_text(), r"(?m)^AiPlayerbot\.WarriorGapCloser = 0$")

    def test_boss_facts_come_from_the_boss_tactics_queries(self):
        text = (WARRIOR / "WarriorGapCloser.cpp").read_text()
        # The dps hold is the boss tactics' own query, not a copy of its rule.
        self.assertIn("f.dpsHeld = IsDpsHeld(ai, bot, unit);", text)
        self.assertIn("f.bossPullbackOrStand = BossPullbackOrStand(ai, bot, unit);", text)
        self.assertIn("f.tankHoldsBoss = tankHoldsBoss;", text)
        self.assertIn("FindBossStand(ai, bot).valid", text)
        self.assertIn("GetVictim() == bot && IsBossUnit(attacker)", text)


if __name__ == "__main__":
    unittest.main()
