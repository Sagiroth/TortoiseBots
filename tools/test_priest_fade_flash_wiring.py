"""Priest healer rows approved by the owner on 2026-10-14.

Zul'Farrak hire run: a holy priest went 3555 -> 1354 mana in 38 s and held top
threat for 28.5 s on a 7-mob pull the tank could not hold. Two rows answer it:
1. a mob on the priest (group only, via CastFadeAction) fires Fade above every
   heal row;
2. while two or more attackers are on someone other than a tank, Flash Heal
   sits just above Greater Heal on the low/medium party rows and the self row.

Run: python3 -m unittest tools/test_priest_fade_flash_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRIEST = ROOT / "ai/playerbot/strategy/priest"


def rows(path):
    """(trigger, action, relevance expression) for every single-action row."""
    text = (PRIEST / path).read_text()
    pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(text)]


class PriestFadeFlashWiringTest(unittest.TestCase):
    def test_fade_above_heals_when_targeted(self):
        self.assertIn(("target of attacker", "fade", "ACTION_CRITICAL_HEAL + 3"), rows("PriestStrategy.cpp"))
        actions = (PRIEST / "PriestActions.h").read_text()
        self.assertIn('BUFF_ACTION_U(CastFadeAction, "fade", bot->GetGroup());', actions)

    def test_flash_heal_rows(self):
        self.assertIn(("low health loose pack", "flash heal", "ACTION_MEDIUM_HEAL + 0.5f"), rows("PriestStrategy.cpp"))
        holy = rows("HolyPriestStrategy.cpp")
        self.assertIn(("party member low health loose pack", "flash heal on party", "ACTION_MEDIUM_HEAL + 2.5f"), holy)
        self.assertIn(("party member medium health loose pack", "flash heal on party", "ACTION_MEDIUM_HEAL + 0.5f"), holy)
        disc = rows("DisciplinePriestStrategy.cpp")
        self.assertIn(("party member low health loose pack", "flash heal on party", "ACTION_MEDIUM_HEAL + 1.5f"), disc)
        self.assertIn(("party member medium health loose pack", "flash heal on party", "ACTION_MEDIUM_HEAL + 0.5f"), disc)

    def test_triggers_registered_and_gated(self):
        ctx = (PRIEST / "PriestAiObjectContext.cpp").read_text()
        for name in ("party member low health loose pack", "party member medium health loose pack", "low health loose pack"):
            self.assertIn('creators["%s"]' % name, ctx)
        trig = (PRIEST / "PriestTriggers.cpp").read_text()
        fn = trig[trig.index("bool ai::PriestPackLoose("):]
        fn = fn[:fn.index("\n}\n")]
        self.assertIn("bot->GetGroup()", fn)
        self.assertIn("PlayerbotAI::IsTank(victimPlayer)", fn)
        self.assertIn("++offTank >= 2", fn)


if __name__ == "__main__":
    unittest.main()
