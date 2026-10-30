"""Backlog 32 (pip S3/S5, 2026-10-27): `speed=` refusals name the reason that holds for anyone first, and
`speed=` stands anywhere among the start's words (after `player tank <Bot>` too).

The policy (runtime/DungeonRunPolicy.h CheckTestSpeed, tested in tools/test_dungeon_run_policy.cpp) decides the
order; these checks pin that the glue in commands/Commander.cpp takes `speed=` out before the player-led words and
hands the ordered refusal what it matches on.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands" / "Commander.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RunStartSpeedWiringTest(unittest.TestCase):
    def test_speed_comes_out_before_the_player_led_words(self):
        run = body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        self.assertIn("TortoiseBots::PlayerLed::TakeStart(speedArgs.rest)", run)
        self.assertNotIn("TakeStart(rawArg)", run)
        self.assertLess(run.index("TakeSpeed(rawArg)"), run.index("TakeStart(speedArgs.rest)"))
        self.assertEqual(run.count("TakeSpeed("), 1)

    def test_the_ordered_refusal_lives_in_the_policy(self):
        run = body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        self.assertIn("CheckTestSpeed(speedArgs.rate, playerLedStart.playerLed, realPlayer, IsBotAdministrator(Requester(handler)))", run)
        self.assertNotIn('"forbidden", "Only a GameMaster may start a test-speed run', run)

    def test_player_led_start_keeps_its_own_words(self):
        run = body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")
        self.assertIn("if (!playerLedStart.playerLed)\n            arg = LowerCopy(speedArgs.rest);", run)


if __name__ == "__main__":
    unittest.main()
