"""`cmd revive party` completes a headless bot's release at once.

Player::ScheduleRepopAtGraveyard only arms a flag for a connected headless
session, and the core fires it once the bot's movement acks settle, seconds
later. A resurrect in that window leaves the flag armed, and the repop then
far-teleports a living bot out of the instance (crash 2026-11-03 root cause).
The release branch therefore runs the repop itself, while the bot is still a
corpse; the core's entrance path puts a dungeon bot back alive at the door.

The repop resurrects first and sends the entrance near teleport second, and a
near teleport only moves the bot on its ack. BotManager acks after the world
update, so a bot left unacked stands alive at its corpse for one map update and
a boss re-aggros the revived raid (walk 92, Hakkar regroup). The same call
stack must ack the near teleport at once.
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
        # Never while a far teleport is already under way: it repops there
        # already, and a second TeleportTo would be issued.
        self.assertLess(block.index("!member->IsBeingTeleportedFar()"), repop)

    def test_headless_repop_acks_the_entrance_near_teleport_at_once(self):
        block = body(COMMANDER, 'if (verb == "revive" && parts.size() == 1)')
        repop = block.index("member->RepopAtGraveyard();")
        ack = block.index("memberAi->HandleTeleportAck();")
        # The ack must stay inside the same call stack as the repop: the bot
        # must never be alive at its corpse when the next map update runs.
        self.assertLess(repop, ack)
        tail = block[repop:ack]
        self.assertNotIn("++revived", tail)
        # Guarded: ack only a living bot whose entrance teleport is pending and
        # near. Any other teleport state is BotManager's to ack.
        guard = tail.rindex("if (")
        self.assertIn("member->IsAlive()", tail[guard:])
        self.assertIn("member->IsBeingTeleportedNear()", tail[guard:])


if __name__ == "__main__":
    unittest.main()
