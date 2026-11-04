"""Resto shaman cooldown rows (combat-t5, spec section 5 T5).

S1: Spirit Link fires only when the tank is low (low-health save, not a pull
buff); S2: Ancestral Swiftness is held for the critical row, never popped on
the bare buff trigger.

Run: python3 -m unittest tools/test_resto_shaman_cooldown_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHAMAN = ROOT / "ai/playerbot/strategy/shaman"


def rows(path):
    """(trigger, action, relevance expression) for every single-action row."""
    text = (SHAMAN / path).read_text()
    pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(text)]


class RestoShamanCooldownWiringTest(unittest.TestCase):
    def test_spirit_link_gated_row(self):
        combat = rows("RestorationShamanStrategy.cpp")
        self.assertIn(
            ("spirit link on party tank", "spirit link on party tank", "ACTION_CRITICAL_HEAL + 2"),
            combat)
        triggers = (SHAMAN / "ShamanTriggers.h").read_text()
        self.assertIn("ShouldSpiritLink", triggers)

    def test_ancestral_swiftness_held_for_critical(self):
        combat = rows("RestorationShamanStrategy.cpp")
        self.assertIn(
            ("party member critical health", "ancestral swiftness", "ACTION_CRITICAL_HEAL + 3"),
            combat)
        self.assertEqual(
            [r for r in combat if r[0] == "ancestral swiftness"], [])
        self.assertIn(
            ("ancestral swiftness heal", "healing wave on party", "ACTION_HIGH + 1"),
            combat)

    def test_no_earth_shield_on_others(self):
        combat = rows("RestorationShamanStrategy.cpp")
        self.assertEqual(
            [r for r in combat if "earth shield" in r[0] or "earth shield" in r[1]], [])
        context = (SHAMAN / "ShamanAiObjectContext.cpp").read_text()
        self.assertNotIn("earth shield on party tank", context)
        self.assertNotIn("earth shield on party", context)
        triggers = (SHAMAN / "ShamanTriggers.h").read_text()
        self.assertNotIn("PartyTankEarthShieldTrigger", triggers)

    def test_resto_keeps_water_shield(self):
        combat = rows("RestorationShamanStrategy.cpp")
        self.assertIn(
            ("water shield", "water shield", "ACTION_HIGH"),
            combat)
        self.assertIn(
            ("water shield", "water shield", "ACTION_NORMAL"),
            combat)


if __name__ == "__main__":
    unittest.main()
