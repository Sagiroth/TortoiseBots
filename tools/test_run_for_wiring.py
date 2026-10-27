"""Wiring pins for `.bot run for <Commander> <verb>` (raid plan task A9: a GameMaster drives a commander's
run from the console).

The pure argument rule (commander name letters only 2-12, one of the six verbs, `player` never in a
start rest) lives in runtime/RemoteCommanderPolicy.h ParseRunFor and is unit-tested in
tools/test_remote_commander_policy.cpp (TestParseRunFor). These pins prove the live code uses it: the GM gate
runs before the parse, a refused parse answers the usage, a missing commander answers `no-commander`, and
the run's words run as the commander himself (today's HandleDungeonRun body moved to RunVerbAs, called
with the requester for the in-game path).
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RunForWiringTest(unittest.TestCase):
    def test_for_dispatches_before_the_in_game_checks(self):
        run = body(COMMANDS, "bool HandleDungeonRun(", "\nvoid UpdateRemoteCommanders(")
        needle = 'if (verb == "for")\n        return HandleRunFor(handler, rawArg, intent);'
        self.assertIn(needle, run)
        self.assertLess(run.index(needle), run.index('if (verb != "start"'))
        self.assertIn("return RunVerbAs(handler, Requester(handler), verb, rawArg, intent);", run)

    def test_gm_gated_before_the_parse(self):
        fn = body(COMMANDS, "static bool HandleRunFor(")
        self.assertLess(fn.index("IsGmCliHandler(handler)"), fn.index("ParseRunFor("))
        self.assertLess(fn.index("ParseRunFor("), fn.index("FindPlayerByName("))
        self.assertIn('"forbidden", "Only a GameMaster may drive another commander\'s run."', fn)
        self.assertIn('"no-commander"', fn)
        self.assertIn('RunVerbAs(handler, commander, verb, rest, "run " + verb)', fn)

    def test_run_words_run_as_the_commander(self):
        self.assertIn("static bool RunVerbAs(", COMMANDS)
        verb = body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{")
        self.assertIn("RemoteCommander::RunSurfaceOn(", verb)
        self.assertIn("if (!commander || !commander->GetSession())", verb)

    def test_documented(self):
        cmd = (ROOT / "docs/play/commands/commander.md").read_text()
        run = (ROOT / "docs/play/dungeon-run-leader.md").read_text()
        self.assertIn("`.bot run for", cmd)
        self.assertIn("`.bot run for", run)


if __name__ == "__main__":
    unittest.main()
