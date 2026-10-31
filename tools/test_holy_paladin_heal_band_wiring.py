"""Holy paladin party medium-band heal row (combat spec 02).

The 50-70 % party band belongs to Holy Light: in raid trash the hurt members sit
between 50 and 90 % almost all the time, so answering every wound with a
~400-point Flash of Light halves healing against priests landing 1,000-2,000
point heals on the same targets. The paladin's own 50-70 % row stays a cheap
Flash of Light (plain cast, no mana gate), Flash of Light stays the 70-90 %
top-up, and out of combat a cheap FoL top-up is still right.

Run: python3 -m unittest tools/test_holy_paladin_heal_band_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy/paladin/HolyPaladinStrategy.cpp"
STRATEGY_H = ROOT / "ai/playerbot/strategy/Strategy.h"

ROW = re.compile(
    r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)'
)

HEAL_ROW = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0,(.*?)NULL\)\)\);', re.S)
ENTRY = re.compile(r'new NextAction\("([^"]+)",\s*([^)]+)\)')
ACTION_VALUE = {name: int(v) for name, v in re.findall(r"(ACTION_\w+)\s*=\s*(\d+)", STRATEGY_H.read_text())}


def method_body(cls, method):
    """Body of `void <cls>::<method>(...)` (brace-matched)."""
    text = STRATEGY.read_text()
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


class HolyPaladinHealBandWiringTest(unittest.TestCase):
    def test_medium_band_party_is_holy_light_self_stays_flash(self):
        combat = rows(method_body("HolyPaladinStrategy", "InitCombatTriggers"))
        self.assertIn(("medium health", "flash of light", "ACTION_MEDIUM_HEAL"), combat)
        self.assertIn(("party member medium health", "holy light on party",
                       "ACTION_MEDIUM_HEAL"), combat)

    def test_medium_band_single_row_per_trigger(self):
        combat = rows(method_body("HolyPaladinStrategy", "InitCombatTriggers"))
        for trigger in ("medium health", "party member medium health"):
            self.assertEqual(sum(1 for t, _, _ in combat if t == trigger), 1, trigger)

    def test_almost_full_stays_flash_of_light(self):
        ranked = ranked_actions(method_body("HolyPaladinStrategy", "InitCombatTriggers"))
        for trigger, flash, party_flash in (
                ("almost full health", "flash of light", None),
                ("party member almost full health", None, "flash of light on party")):
            relevance = {action: value for t, action, value in ranked if t == trigger}
            want = party_flash if party_flash else flash
            self.assertIn(want, relevance, trigger)
            self.assertEqual(relevance[want], ACTION_VALUE["ACTION_LIGHT_HEAL"], trigger)

    def test_non_combat_medium_stays_flash_of_light(self):
        noncombat = rows(method_body("HolyPaladinStrategy", "InitNonCombatTriggers"))
        self.assertIn(("medium health", "flash of light", "ACTION_HIGH"), noncombat)
        self.assertIn(("party member medium health", "flash of light on party",
                       "ACTION_HIGH"), noncombat)


if __name__ == "__main__":
    unittest.main()
