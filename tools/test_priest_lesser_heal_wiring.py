"""A priest that knows Heal stops casting Lesser Heal.

A level 16+ priest has a downrankable Heal line (and Renew for the smallest
wounds), so the Lesser Heal actions gate themselves off once any Heal rank
is trained. The trigger rows and ActionNode fallbacks keep naming
"lesser heal on party" for the below-16 priest that still needs it.

Run: python3 -m unittest tools/test_priest_lesser_heal_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRIEST = ROOT / "ai/playerbot/strategy/priest"


class PriestLesserHealWiringTest(unittest.TestCase):
    def test_macros_replaced_by_explicit_classes(self):
        actions = (PRIEST / "PriestActions.h").read_text()
        self.assertNotIn("HEAL_ACTION(CastLesserHealAction", actions)
        self.assertNotIn("HEAL_PARTY_ACTION_E(CastLesserHealOnPartyAction", actions)

    def test_isuseful_gates_on_heal_line(self):
        actions = (PRIEST / "PriestActions.h").read_text()
        for cls, base in (("CastLesserHealAction", "CastHealingSpellAction"),
                          ("CastLesserHealOnPartyAction", "HealPartyMemberAction")):
            body = actions[actions.index("class " + cls):]
            body = body[:body.index("\n    };")]
            self.assertIn("PriestKnowsHealLine(bot)", body, cls)
            self.assertIn("return " + base + "::isUseful();", body, cls)
            self.assertLess(body.index("PriestKnowsHealLine(bot)"),
                            body.index(base + "::isUseful"), cls)

    def test_trigger_rows_unchanged(self):
        for path in ("HolyPriestStrategy.cpp", "DisciplinePriestStrategy.cpp"):
            self.assertIn('"lesser heal on party"', (PRIEST / path).read_text(), path)


if __name__ == "__main__":
    unittest.main()
