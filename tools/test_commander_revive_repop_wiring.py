"""`cmd revive party` completes a headless bot's release at once.

Player::ScheduleRepopAtGraveyard only arms a flag for a connected headless
session, and the core fires it once the bot's movement acks settle, seconds
later. A resurrect in that window leaves the flag armed, and the repop then
far-teleports a living bot out of the instance (crash 2026-11-03 root cause).
The release branch therefore runs the repop itself, while the bot is still a
corpse; the core's entrance path puts a dungeon bot back alive at the door.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RevivePartyRepopTest(unittest.TestCase):
    def test_headless_release_repops_before_the_flag_can_fire(self):
        block = body(COMMANDER, 'if (verb == "revive" && parts.size() == 1)')
        release = block.index('ExecuteQuietAction(memberAi, "release",')
        repop = block.index("member->RepopAtGraveyard();")
        self.assertLess(release, repop)
        guard = block.index("IsLiveHeadlessBot(member, memberRecord)")
        self.assertLess(release, guard)
        self.assertLess(guard, repop)
        # Never for a living bot: RepopAtGraveyard would far-teleport it.
        self.assertLess(block.index("!member->IsAlive()"), repop)


if __name__ == "__main__":
    unittest.main()
