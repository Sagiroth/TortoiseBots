"""Holy paladin off-DPS wiring: the `offdps` placeholder and the
`offdps pve/pvp/raid` situation strategies resolve, and the damage rows sit
behind the shared `healer should attack` gate at the lowest relevance so
every heal outbids them.

Run: python3 -m unittest tools/test_paladin_offdps_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PALADIN = ROOT / "ai/playerbot/strategy/paladin"


def offdps_block():
    """The PaladinOffdpsStrategy::InitCombatTriggers body."""
    text = (PALADIN / "PaladinStrategy.cpp").read_text()
    start = text.index("void PaladinOffdpsStrategy::InitCombatTriggers")
    end = text.index("void PaladinOffdpsStrategy::InitNonCombatTriggers")
    return text[start:end]


def rows(block):
    """(trigger, action, relevance) for every single-action row in the block."""
    pat = re.compile(r'TriggerNode\(\s*"([^"]+)",\s*NextAction::array\(0, new NextAction\("([^"]+)", ([^)]+)\), NULL\)\)')
    return [(m.group(1), m.group(2), m.group(3).strip()) for m in pat.finditer(block)]


def basket(block):
    """(action, relevance) alternatives of the multi-action `healer should attack` row."""
    pat = re.compile(r'TriggerNode\(\s*"healer should attack",\s*NextAction::array\(0,(.*?NULL)\)\)', re.S)
    body = pat.search(block).group(1)
    return re.findall(r'new NextAction\("([^"]+)", ([^)]+)\)', body)


class PaladinOffdpsWiringTest(unittest.TestCase):
    def test_placeholder_and_situations_registered(self):
        ctx = (PALADIN / "PaladinAiObjectContext.cpp").read_text()
        self.assertIn('creators["offdps"]', ctx)
        self.assertIn('creators["offdps pve"]', ctx)
        self.assertIn('creators["offdps pvp"]', ctx)
        self.assertIn('creators["offdps raid"]', ctx)
        self.assertIn("OffdpsSituationStrategyFactoryInternal", ctx)

    def test_damage_basket_behind_healer_gate(self):
        alts = [(a, r.strip()) for (a, r) in basket(offdps_block())]
        self.assertEqual([a for (a, _r) in alts],
                         ["hammer of wrath", "holy shock", "judgement"])
        for _a, relevance in alts:
            self.assertTrue(relevance.startswith("ACTION_DEFAULT"),
                            relevance)

    def test_exorcism_needs_undead_or_demon(self):
        block = offdps_block()
        self.assertIn(("exorcism and healer should attack", "exorcism",
                       "ACTION_DEFAULT + 0.2f"), rows(block))
        ctx = (PALADIN / "PaladinAiObjectContext.cpp").read_text()
        self.assertIn('creators["exorcism and healer should attack"]', ctx)

    def test_consecration_into_packs_only(self):
        block = offdps_block()
        self.assertIn(("melee medium aoe and healer should attack",
                       "consecration", "ACTION_DEFAULT + 0.6f"), rows(block))

    def test_no_row_outbids_a_heal(self):
        pat = re.compile(r'new NextAction\("[^"]+", ([^)]+)\)')
        for relevance in pat.findall(offdps_block()):
            self.assertTrue(relevance.strip().startswith("ACTION_DEFAULT"),
                            relevance)


if __name__ == "__main__":
    unittest.main()
