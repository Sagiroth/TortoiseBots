"""Shield tank stance wiring (issue #1553): a levelling Arms/Fury warrior
tanking in Defensive Stance, or a druid in Bear/Dire Bear Form, counts as a
tank at the shield gate even when the talent-role read says DPS.

Run: python3 -m unittest tools/test_shield_tank_stance_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = ROOT / "ai/playerbot/strategy/priest/PriestActions.h"


def shield_body():
    text = ACTIONS.read_text()
    body = text[text.index("class CastPowerWordShieldOnPartyAction"):]
    return body[:body.index("\n    };")]


class ShieldTankStanceWiringTest(unittest.TestCase):
    def test_stance_form_tank_uses_policy(self):
        # The gate counts stance/form tanks through the pure policy, so a
        # warrior in Defensive Stance or a druid in Bear Form is spared the
        # shield above the critical band.
        body = shield_body()
        self.assertIn("TankForShield", body)
        self.assertIn("71", body)
        self.assertIn("5487", body)
        self.assertIn("9634", body)
        self.assertIn("CLASS_WARRIOR", body)
        self.assertIn("CLASS_DRUID", body)


if __name__ == "__main__":
    unittest.main()
