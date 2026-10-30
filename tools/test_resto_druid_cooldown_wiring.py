"""Resto druid cooldown rows (combat spec T4: D1, D2).

D1: Tranquility is a 30-min channel for group damage, not for two scratches:
no ("medium aoe heal", "tranquility") row in the base combat triggers; the
group-sized row in the AoE strategy ("medium group heal setting") keeps it.

D2: Nature's Swiftness is held for the critical row, not popped on the first
quiet tick: no ("nature's swiftness", ACTION_HIGH) row in the boost strategy;
the critical pop row and the instant-Healing-Touch spend row stay.

D3: Regrowth is not recast while its own HoT runs, so each heal band from 20%
to 70% ranks Healing Touch below Regrowth as the fallthrough (self and party).

Run: python3 -m unittest tools/test_resto_druid_cooldown_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy/druid/RestorationDruidStrategy.cpp"
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


class RestoDruidCooldownWiringTest(unittest.TestCase):
    def test_tranquility_not_on_two_scratches(self):
        base = rows(method_body("RestorationDruidStrategy", "InitCombatTriggers"))
        self.assertNotIn(
            ("medium aoe heal", "tranquility", "ACTION_MEDIUM_HEAL + 2"), base)
        for trigger, action, _ in base:
            self.assertFalse(
                trigger == "medium aoe heal" and action == "tranquility",
                "tranquility still queued on the 2-member aoe-heal trigger")
        aoe = rows(method_body("RestorationDruidAoeStrategy", "InitCombatTriggers"))
        self.assertIn(
            ("medium group heal setting", "tranquility", "ACTION_MEDIUM_HEAL + 2"), aoe)

    def test_natures_swiftness_held_for_critical(self):
        boost = rows(method_body("RestorationDruidBoostStrategy", "InitCombatTriggers"))
        for trigger, action, relevance in boost:
            self.assertFalse(
                action == "nature's swiftness" and relevance == "ACTION_HIGH",
                "nature's swiftness still popped on the bare boost trigger")
        base = rows(method_body("RestorationDruidStrategy", "InitCombatTriggers"))
        self.assertIn(
            ("party member critical health", "nature's swiftness",
             "ACTION_CRITICAL_HEAL + 4"), base)
        self.assertIn(
            ("nature's swiftness active", "healing touch on party",
             "ACTION_CRITICAL_HEAL + 3"), base)

    def test_healing_touch_fills_regrowth_gap_in_heal_bands(self):
        ranked = ranked_actions(method_body("RestorationDruidStrategy", "InitCombatTriggers"))
        for trigger, regrowth, healing_touch in (
                ("low health", "regrowth", "healing touch"),
                ("medium health", "regrowth", "healing touch"),
                ("party member low health", "regrowth on party", "healing touch on party"),
                ("party member medium health", "regrowth on party", "healing touch on party")):
            relevance = {action: value for t, action, value in ranked if t == trigger}
            self.assertIn(healing_touch, relevance, trigger)
            self.assertLess(relevance[healing_touch], relevance[regrowth], trigger)


if __name__ == "__main__":
    unittest.main()
