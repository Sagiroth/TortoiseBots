"""The decision capture's glue: where commands/Commander.cpp starts, wraps and
ends a capture, and that the shipped default is off.

The service and the file side are tested in tools/test_decision_capture_files.cpp;
this pins the call sites:
- Begin at `.bot run start`, after the run slot exists and before its first tick;
- BeforeTick after the facts are complete (ScopeStandPullFacts, liveTactics,
  ReadRunPlayers, ApplyPlayerFacts) and directly before DungeonRun::Tick;
- AfterTick directly after it and before TakeSay clears the run's say;
- neither reached by the player-led tick (PlayerLed::Tick);
- End in EraseRunSlot and EndAll in EraseAllRunSlots, the two places a run's
  slot goes away;
- AiPlayerbot.DecisionCapture.Enabled = 0 in the shipped template.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands" / "Commander.cpp").read_text()
CONF = (ROOT / "ai" / "playerbot" / "aiplayerbot.conf.dist.in").read_text()

CAPTURE = "TortoiseBots::DecisionCaptureService::Instance()"


def function(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end]


class TickHookTest(unittest.TestCase):
    def setUp(self):
        self.tick = function(COMMANDER, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)")

    def test_facts_are_complete_before_the_capture(self):
        tick = self.tick
        before = tick.index("BeforeTick(")
        self.assertLess(tick.index("ScopeStandPullFacts("), before)
        self.assertLess(tick.index("liveTactics = &ai::GetBossTactics()"), before)
        self.assertLess(tick.index("ReadRunPlayers("), before)
        self.assertLess(tick.index("ApplyPlayerFacts("), before)
        self.assertLess(before, tick.index("DungeonRun::Tick(slot.run, facts, dt)"))

    def test_before_tick_is_the_last_statement_before_the_tick(self):
        tick = self.tick
        before = tick.index(CAPTURE + ".BeforeTick(")
        call = tick.index("DungeonRun::Tick(slot.run, facts, dt)")
        between = tick[before:call]
        self.assertEqual(between.count(";"), 1, between)
        self.assertEqual(between.count(".BeforeTick("), 1, between)

    def test_after_tick_sits_between_the_tick_and_take_say(self):
        tick = self.tick
        call = tick.index("TortoiseBots::DungeonRun::Tick(slot.run, facts, dt)")
        after = tick.index(CAPTURE + ".AfterTick(")
        self.assertLess(call, after)
        self.assertLess(after, tick.index("TakeSay("))
        self.assertIn("AfterTick(commanderRaw, slot.run, order)", tick[after:after + 100])

    def test_player_led_tick_is_not_captured(self):
        tick = self.tick
        led = tick.index("PlayerLed::Tick(")
        before = tick.index("BeforeTick(")
        self.assertLess(led, before)
        branch = tick[led:before]
        # The capture stands in the else branch: the last `else` before it.
        self.assertIn("\n    else\n", branch)
        self.assertEqual(tick.count("BeforeTick("), 1)


class StartAndEndTest(unittest.TestCase):
    def test_begin_runs_at_start_after_the_slot_exists(self):
        slot = COMMANDER.index("DungeonRuns()[commanderRaw] = slot;")
        begin = COMMANDER.index(CAPTURE + ".Begin(")
        self.assertLess(slot, begin)
        self.assertIn("commanderRaw, commander->GetName(), plan->name", COMMANDER[begin:begin + 120])
        self.assertIn('" (decision capture " + captureId + ")"', COMMANDER)

    def test_erase_run_slot_ends_the_capture(self):
        erase = function(COMMANDER, "void EraseRunSlot(uint64_t commanderRaw)")
        self.assertIn(CAPTURE + '.End(commanderRaw, "slot released")', erase)

    def test_erase_all_run_slots_ends_every_capture(self):
        erase = function(COMMANDER, "void EraseAllRunSlots()")
        self.assertIn(CAPTURE + '.EndAll("all slots released")', erase)


class ConfigTest(unittest.TestCase):
    def test_shipped_default_is_off(self):
        self.assertEqual(CONF.count("#AiPlayerbot.DecisionCapture.Enabled = 0"), 1)


if __name__ == "__main__":
    unittest.main()
