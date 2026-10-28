"""Item 1 (`until boss|step N|leg X`): the glue in commands/Commander.cpp takes the until clause out of the start's
words, refuses it for a player-led run, resolves it against the plan's bosses (ai::GetDungeonPlans().IsBossEntry) and
sets the stop on the slot before the slot is stored.

The policy (runtime/DungeonRunPolicy.h TakeUntil, ResolveUntilStep, StopAtUntil) decides the behaviour; these checks
pin the wiring, the way tools/test_run_start_speed_wiring.py pins `speed=`.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands" / "Commander.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class RunUntilWiringTest(unittest.TestCase):
    def run_verb(self):
        return body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n")

    def test_until_comes_out_before_the_start_words_are_parsed(self):
        run = self.run_verb()
        self.assertIn("UntilArgs const untilArgs = TakeUntil(arg);", run)
        self.assertLess(run.index("TakeUntil(arg)"), run.index("ParseStartArgs(arg)"))
        self.assertIn("arg = untilArgs.rest;", run)

    def test_a_player_led_start_refuses_until(self):
        run = self.run_verb()
        self.assertIn("if (playerLedStart.playerLed && untilArgs.given)", run)
        self.assertIn("until is for bot-led runs", run)

    def test_the_stop_is_resolved_against_the_plan_map_bosses(self):
        run = self.run_verb()
        self.assertIn("ResolveUntilStep(*plan, untilArgs.kind, untilArgs.value, startStep,", run)
        self.assertIn("ai::GetDungeonPlans().IsBossEntry(plan->map, entry)", run)
        self.assertIn('"until-at-start"', run)

    def test_the_stop_is_set_on_the_slot_before_it_is_stored(self):
        run = self.run_verb()
        self.assertIn("slot.run.untilStep = untilStop;", run)
        self.assertIn("slot.run.untilText = untilArgs.text;", run)
        self.assertLess(run.index("slot.run.untilStep = untilStop;"), run.index("DungeonRuns()[commanderRaw] = slot"))


if __name__ == "__main__":
    unittest.main()
