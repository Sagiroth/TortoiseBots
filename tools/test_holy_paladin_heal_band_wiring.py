"""Holy paladin combat seal row: ranged healers pay no seal GCD (spec 02).

The Holy combat row must name `"seal in melee"`, a SealTrigger that also
requires melee reach on the current target, so a holy paladin healing from
range never recasts Seal of Wisdom between Judgement-upkeep seals. Ret/Prot
keep the bare `"seal"` row; the medium party row still names
`"holy light on party"` and the 70-90 % row `"flash of light on party"`.

Run: python3 -m unittest tools/test_holy_paladin_heal_band_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STRATEGY = ROOT / "ai/playerbot/strategy/paladin/HolyPaladinStrategy.cpp"
STRATEGY_H = ROOT / "ai/playerbot/strategy/Strategy.h"
TRIGGERS_H = ROOT / "ai/playerbot/strategy/paladin/PaladinTriggers.h"
TRIGGERS_CPP = ROOT / "ai/playerbot/strategy/paladin/PaladinTriggers.cpp"
CONTEXT = ROOT / "ai/playerbot/strategy/paladin/PaladinAiObjectContext.cpp"
RET = ROOT / "ai/playerbot/strategy/paladin/RetributionPaladinStrategy.cpp"
PROT = ROOT / "ai/playerbot/strategy/paladin/ProtectionPaladinStrategy.cpp"

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


class HolyPaladinHealBandWiringTest(unittest.TestCase):
    def test_medium_band_party_is_holy_light_self_stays_flash(self):
        combat = rows(method_body(STRATEGY, "HolyPaladinStrategy", "InitCombatTriggers"))
        self.assertIn(("medium health", "flash of light", "ACTION_MEDIUM_HEAL"), combat)
        self.assertIn(("party member medium health", "holy light on party",
                       "ACTION_MEDIUM_HEAL"), combat)

    def test_medium_band_single_row_per_trigger(self):
        combat = rows(method_body(STRATEGY, "HolyPaladinStrategy", "InitCombatTriggers"))
        for trigger in ("medium health", "party member medium health"):
            self.assertEqual(sum(1 for t, _, _ in combat if t == trigger), 1, trigger)

    def test_almost_full_stays_flash_of_light(self):
        ranked = ranked_actions(method_body(STRATEGY, "HolyPaladinStrategy", "InitCombatTriggers"))
        for trigger, flash, party_flash in (
                ("almost full health", "flash of light", None),
                ("party member almost full health", None, "flash of light on party")):
            relevance = {action: value for t, action, value in ranked if t == trigger}
            want = party_flash if party_flash else flash
            self.assertIn(want, relevance, trigger)
            self.assertEqual(relevance[want], ACTION_VALUE["ACTION_LIGHT_HEAL"], trigger)

    def test_non_combat_medium_stays_flash_of_light(self):
        noncombat = rows(method_body(STRATEGY, "HolyPaladinStrategy", "InitNonCombatTriggers"))
        self.assertIn(("medium health", "flash of light", "ACTION_HIGH"), noncombat)
        self.assertIn(("party member medium health", "flash of light on party",
                       "ACTION_HIGH"), noncombat)

    def test_holy_seal_row_needs_melee(self):
        combat = rows(method_body(STRATEGY, "HolyPaladinStrategy", "InitCombatTriggers"))
        self.assertIn(("seal in melee", "seal of wisdom", "ACTION_NORMAL + 2"), combat)
        self.assertFalse(any(trigger == "seal" for trigger, _, _ in combat), "bare seal row")

    def test_seal_in_melee_trigger_checks_reach(self):
        decl = TRIGGERS_H.read_text()
        self.assertRegex(decl, r"class SealInMeleeTrigger\s*:\s*public SealTrigger")
        impl = TRIGGERS_CPP.read_text()
        start = impl.index("SealInMeleeTrigger::IsActive")
        open_brace = impl.index("{", start)
        depth = 0
        for i in range(open_brace, len(impl)):
            if impl[i] == "{":
                depth += 1
            elif impl[i] == "}":
                depth -= 1
                if depth == 0:
                    body = impl[open_brace:i + 1]
                    break
        else:
            self.fail("unbalanced braces in SealInMeleeTrigger::IsActive")
        self.assertIn("SealTrigger::IsActive()", body)
        self.assertIn("CanReachWithMeleeAutoAttack", body)
        self.assertIn('"current target"', body)

    def test_seal_in_melee_trigger_registered(self):
        self.assertIn('"seal in melee"', CONTEXT.read_text())

    def test_ret_prot_keep_bare_seal(self):
        for path, cls in ((RET, "RetributionPaladinStrategy"), (PROT, "ProtectionPaladinStrategy")):
            combat = rows(method_body(path, cls, "InitCombatTriggers"))
            self.assertTrue(any(trigger == "seal" for trigger, _, _ in combat), path.name)

    def test_heal_bands_unchanged(self):
        combat = rows(method_body(STRATEGY, "HolyPaladinStrategy", "InitCombatTriggers"))
        self.assertIn(("party member medium health", "holy light on party",
                       "ACTION_MEDIUM_HEAL"), combat)
        self.assertIn(("party member almost full health", "flash of light on party",
                       "ACTION_LIGHT_HEAL"), combat)

if __name__ == "__main__":
    unittest.main()
