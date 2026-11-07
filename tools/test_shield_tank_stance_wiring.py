"""Shield tank stance wiring (issue #1553): a levelling Arms/Fury warrior
tanking in Defensive Stance, or a druid in Bear/Dire Bear Form, counts as a
tank at the shield gate even when the talent-role read says DPS. Round 2: an
explicitly assigned non-tank role wins over the stance.

Run: python3 -m unittest tools/test_shield_tank_stance_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = ROOT / "ai/playerbot/strategy/priest/PriestActions.h"


def shield_body():
    text = ACTIONS.read_text()
    body = text[text.index("class CastPowerWordShieldOnPartyAction"):]
    return body[:body.index("\n    };")]


class ShieldTankStanceWiringTest(unittest.TestCase):
    def test_warrior_stance_term(self):
        # CLASS_WARRIOR stays paired with aura 71 through &&: swapping the
        # class or weakening && to || breaks this pin.
        body = shield_body()
        self.assertRegex(
            body,
            r"target->IsPlayer\(\)\s*&&\s*\(\(Player\*\)target\)->GetClass\(\)"
            r"\s*==\s*CLASS_WARRIOR\s*&&\s*ai->HasAura\(71,",
        )

    def test_druid_form_term(self):
        # CLASS_DRUID stays paired with Bear 5487 or Dire Bear 9634 through
        # &&: swapping the class or weakening && to || breaks this pin.
        body = shield_body()
        self.assertRegex(
            body,
            r"target->IsPlayer\(\)\s*&&\s*\(\(Player\*\)target\)->GetClass\(\)"
            r"\s*==\s*CLASS_DRUID\s*&&\s*\(ai->HasAura\(5487,\s*target\)"
            r"\s*\|\|\s*ai->HasAura\(9634,",
        )

    def test_forced_role_wins(self):
        # The stance/form fallback reads the same forced role IsTank uses:
        # the player-wide role and the bot AI's role. Dropping either read
        # breaks this pin.
        body = shield_body()
        self.assertRegex(
            body,
            r"PlayerbotAIStorage::Instance\(\)\.GetPlayerForcedRole"
            r"\(\(\(Player\*\)target\)->GetObjectGuid\(\)\)",
        )
        self.assertRegex(
            body,
            r"PlayerbotAIStorage::Instance\(\)\.GetAI\(\(Player\*\)target\)"
            r"[\s\S]*?->GetForcedRole\(\)",
        )
        self.assertIn("BOT_ROLE_NONE", body)
        # The forced-role veto feeds TankForShield between the role read and
        # the stance checks (second argument, not the aura terms).
        self.assertRegex(
            body,
            r"TankForShield\([\s\S]*?IsTank\(\(Player\*\)target"
            r"[\s\S]*?GetPlayerForcedRole[\s\S]*?CLASS_WARRIOR",
        )


if __name__ == "__main__":
    unittest.main()
