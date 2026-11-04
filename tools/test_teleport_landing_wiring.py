"""A near teleport lands a bot on the floor under its destination.

Banish 24466 (Jin'do) teleports a player to z 90 over the ZG pit floor at
77.6. A player's client makes it fall; a bot has no client, so its server
position stayed at z 90 and it never pathed out (walk 36: 99 bot-instances,
about 23 bot-hours at the spot, melee bots at 0 damage). PlayerbotAI's
IsBeingTeleportedNear() branch now probes the floor with the jump landing's own
feet probe and starts the jump landing's simulated fall, behind
AiPlayerbot.TeleportLandingFall (off by default: it changes every near
teleport).
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
AI = (ROOT / "ai/playerbot/PlayerbotAI.cpp").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TeleportLandingWiringTest(unittest.TestCase):
    def test_the_near_branch_lands_on_the_floor_under_the_destination(self):
        fn = body(AI, "void PlayerbotAI::HandleTeleportAck()", "\nvoid PlayerbotAI::Reset(")
        near = body(fn, "if (bot->IsBeingTeleportedNear())", "else if (bot->IsBeingTeleportedFar())")
        # The ack first (the core moves the bot), then the landing read off the new position.
        self.assertLess(near.index("HandleMoveTeleportAckOpcode(p)"), near.index("sPlayerbotAIConfig.teleportLandingFall"))
        self.assertIn("TeleportLandingZ(", near)
        # The same feet probe the jump landing uses, and the map's floor beside it: the higher walkable floor wins.
        self.assertIn("kFeetProbeDepth", near)
        self.assertIn("ClosestCorrectPoint(", near)
        self.assertIn("UpdateAllowedPositionZ(", near)
        self.assertIn("mapFloorZ", near)
        self.assertIn("sPlayerbotAIConfig.jumpHeightLimit", near)
        # The fall is the jump landing's, not a bare Relocate: the bot is put down when it ends.
        self.assertIn("SetJumpDestination(", near)
        self.assertIn("SetJumpTime(", near)
        self.assertIn("SimulatedFallMs(", near)

    def test_the_key_is_read_and_documented_off_by_default(self):
        self.assertIn("config.GetBoolDefault(\"AiPlayerbot.TeleportLandingFall\", false)", CONFIG)
        self.assertIn("bool teleportLandingFall", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.TeleportLandingFall = 0$")


if __name__ == "__main__":
    unittest.main()
