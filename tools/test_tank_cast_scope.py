"""Keep tank diagnostics in the unit-target spell overload after source splits."""
from pathlib import Path
import unittest


class TankCastScopeTest(unittest.TestCase):
    def test_logging_has_a_unit_target(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / "ai/playerbot/PlayerbotAISpells.cpp").read_text()
        unit = source.index("bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target,")
        obj = source.index("bool PlayerbotAI::CastSpell(uint32 spellId, GameObject* goTarget,", unit)
        point = source.index("bool PlayerbotAI::CastSpell(uint32 spellId, float x,", obj)
        self.assertIn('logEvent(this, "TankCast"', source[unit:obj])
        self.assertNotIn('logEvent(this, "TankCast"', source[obj:point])
        self.assertNotIn('logEvent(this, "TankCast"', source[point:])


if __name__ == "__main__":
    unittest.main()
