"""Holy paladin combat waste wiring (ZG underperformer fix 5).

Holy healers must not re-buff blessings in combat and must not spend
Freedom casts on non-tanks:

- Combat blessing rows must be gone from every Holy Blessing combat init.
  Blessings stay a non-combat job (or a rebuff only once one has fallen
  off the tank).
- The "party member rooted" Freedom row must target the tank in the Holy
  combat path: the TriggerNode names "tank rooted" and the NextAction
  names "blessing of freedom on tank" below every medium heal.

Run: python3 -m unittest tools/test_holy_paladin_combat_waste_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HOLY = ROOT / "ai/playerbot/strategy/paladin/HolyPaladinStrategy.cpp"
STRATEGY_H = ROOT / "ai/playerbot/strategy/Strategy.h"
CONTEXT = ROOT / "ai/playerbot/strategy/paladin/PaladinAiObjectContext.cpp"

ROW = re.compile(
    r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)'
)
HEAL_ROW = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0,(.*?)NULL\)\)\);', re.S)
ENTRY = re.compile(r'new NextAction\("([^"]+)",\s*([^)]+)\)')
ACTION_VALUE = {name: int(v) for name, v in re.findall(r"(ACTION_\w+)\s*=\s*(\d+)", STRATEGY_H.read_text())}


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


def rows(body):
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in ROW.finditer(body)]


def ranked_actions(body):
    """(trigger, action, relevance) for every action in every trigger row."""
    out = []
    for m in HEAL_ROW.finditer(body):
        for action, expr in ENTRY.findall(m.group(2)):
            value = re.sub(r"ACTION_\w+", lambda n: str(ACTION_VALUE[n.group(0)]), expr)
            out.append((m.group(1), action, eval(value)))
    return out


HOLY_BLESSING_CLASSES = (
    "HolyPaladinBlessingPveStrategy",
    "HolyPaladinBlessingPvpStrategy",
    "HolyPaladinBlessingRaidStrategy",
)

BLESSING_TRIGGERS = {
    "blessing", "greater blessing",
    "blessing on party", "greater blessing on party",
}


class HolyPaladinCombatWasteWiringTest(unittest.TestCase):
    def test_holy_blessing_combat_has_no_rebuff_rows(self):
        for cls in HOLY_BLESSING_CLASSES:
            combat = rows(method_body(HOLY, cls, "InitCombatTriggers"))
            self.assertEqual(combat, [], "%s must not re-buff in combat" % cls)

    def test_holy_blessing_noncombat_still_buffs(self):
        found = False
        for cls in HOLY_BLESSING_CLASSES:
            noncombat = rows(method_body(HOLY, cls, "InitNonCombatTriggers"))
            if any(trigger in BLESSING_TRIGGERS for trigger, _, _ in noncombat):
                found = True
        self.assertTrue(found, "blessing rows must stay out of combat")

    def test_holy_combat_freedom_targets_tank_below_heals(self):
        combat = ranked_actions(method_body(HOLY, "HolyPaladinCureStrategy", "InitCombatTriggers"))
        freedom = {action: value for t, action, value in combat if t == "tank rooted"}
        self.assertIn("blessing of freedom on tank", freedom)
        self.assertLess(freedom["blessing of freedom on tank"], ACTION_VALUE["ACTION_MEDIUM_HEAL"])
        self.assertFalse(any(t == "party member rooted" for t, _, _ in combat),
                         "blanket party Freedom row must be gone")

    def test_tank_rooted_trigger_registered(self):
        self.assertIn('"tank rooted"', CONTEXT.read_text())


if __name__ == "__main__":
    unittest.main()
