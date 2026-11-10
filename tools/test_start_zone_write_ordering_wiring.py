"""A pool character's start-zone writes land before its headless login reads them.

Goblin and high elf pool characters are created with their custom-island
PlayerInfo coordinates, so RandomBotService rewrites the character position and
its character_homebind row to a mainland start zone of their faction right after
CharacterCreation::CreateCharacter returned. That seam is synchronous
(SaveToDB(false, true, false)), but the bot's headless session starts reading
`characters` and `character_homebind` immediately afterwards, so those two
writes must be direct too.

Queued PExecute made the live server queue them behind a saturated character DB
(live 2026-10-21, 09:54-09:56: 25 "INSERT INTO character_homebind" /
"[1062] Duplicate entry" pairs for pool guids 965978-966596): the login SELECT
still saw no homebind row, Player::_LoadHomeBind inserted one itself and the
late REPLACE then hit the primary key. The ordering itself cannot be observed
from a unit test; this pins the call sites.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SERVICE = (ROOT / "runtime/RandomBotService.cpp").read_text()


def creation_block(text):
    i = text.index("if (outcome.result == CHAR_CREATE_SUCCESS)")
    return text[i:text.index("Candidate created;", i)]


class StartZoneWriteOrderingWiringTest(unittest.TestCase):
    def setUp(self):
        self.block = creation_block(SERVICE)

    def test_flexible_race_start_zone_writes_are_direct(self):
        block = self.block
        self.assertIn("if (IsFlexibleStartRace(race))", block)
        self.assertEqual(block.count("CharacterDatabase.DirectPExecute("), 2, block)
        self.assertIn("UPDATE characters SET map = %u, zone = %u,", block)
        self.assertIn("REPLACE INTO character_homebind", block)
        # Never the async queue: it can land after the login SELECT.
        self.assertNotIn("CharacterDatabase.PExecute(", block)


if __name__ == "__main__":
    unittest.main()
