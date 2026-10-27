"""A player's ghost outside no longer drags the bound run's bots in and out
through the portal (pip S2 Deadmines 2026-10-26): the bots hold where they are
while their follow target is outside, and a pause for the lead bot off the
plan's map goes on by itself. A rank-0 commander may stop his own run: `.bot
run stop` is ungated, only `speed=` is GameMaster only.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOVES = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
AREA = (ROOT / "ai/playerbot/strategy/actions/AreaTriggerAction.cpp").read_text()
LASTMOVE = (ROOT / "ai/playerbot/strategy/values/LastMovementValue.h").read_text()
QUERY = (ROOT / "runtime/CommandedGroupQuery.h").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))


def strip_comments(text):
    return "\n".join(line.split("//", 1)[0].rstrip() for line in text.splitlines())


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class PlayerGhostPingpongWiringTest(unittest.TestCase):
    def test_moveto2_refuses_leaving_the_bound_runs_map(self):
        move = strip_comments(body(MOVES, "bool MovementAction::MoveTo2(", "\n}\n"))
        self.assertIn("BoundRunRefusesMapChange(", move)
        self.assertIn("BoundRunPlanMap(bot)", move)
        self.assertIn("MOVE_FAIL_BOUND_RUN_MAP", move)
        self.assertLess(move.index("BoundRunRefusesMapChange("), move.index("ResolveMovePath("))
        self.assertLess(move.index("BoundRunRefusesMapChange("), move.index("HandleSpecialMovement("))

    def test_area_trigger_actions_refuse_leaving(self):
        reach = strip_comments(body(AREA, "bool ReachAreaTriggerAction::Execute(", "\n}\n"))
        self.assertIn("BoundRunRefusesMapChange(", reach)
        self.assertIn("GetAreaTriggerTeleport(", reach)
        fire = strip_comments(body(AREA, "bool AreaTriggerAction::Execute(", "\n}\n"))
        self.assertIn("BoundRunRefusesMapChange(", fire)
        self.assertIn("GetAreaTriggerTeleport(", fire)
        self.assertLess(fire.index("BoundRunRefusesMapChange("), fire.index("HandleAreaTriggerOpcode("))

    def test_bound_run_plan_map_reads_a_running_or_paused_run(self):
        self.assertIn("uint32_t BoundRunPlanMap(Player* bot);", strip_comments(QUERY))
        self.assertIn("RunSlotOf(bot, true)", COMMANDS)
        define = strip_comments(body(COMMANDS, "uint32_t BoundRunPlanMap(Player* bot)", "NoteRunAreaTriggerFired"))
        self.assertIn("slot && slot->run.plan ? slot->run.plan->map : 0", define)
        self.assertNotIn("RunSlotOf(bot, false)", define)

    def test_lead_off_map_pause_is_marked_and_ticked(self):
        tick = strip_comments(body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n"))
        self.assertIn("PauseLeadOffMap(", tick)
        self.assertNotIn('Pause(slot.run, "the lead bot is not on the plan\'s map', tick)
        self.assertIn("LeadBackWhilePaused(", tick)
        back = tick[tick.index("LeadBackWhilePaused(slot.run, back, dt)"):tick.index("LeadBackWhilePaused(slot.run, back, dt)") + 400]
        self.assertIn("lead->GetMapId() == slot.run.plan->map", tick)
        self.assertIn("IsAlive()", tick)
        self.assertLess(tick.index("LeadBackWhilePaused("), tick.index("State::Running || !commander"))

    def test_move_fail_name(self):
        self.assertIn("MOVE_FAIL_BOUND_RUN_MAP = 14,", strip_comments(LASTMOVE))
        self.assertIn('case MOVE_FAIL_BOUND_RUN_MAP: return "bound-run-map";', strip_comments(LASTMOVE))

    def test_run_stop_has_no_gamemaster_gate(self):
        run = strip_comments(body(COMMANDS, "static bool RunVerbAs(ChatHandler* handler, Player* commander, std::string const& verb, std::string const& rawArg,\n    std::string const& intent)\n{", "\n}\n"))
        stop = body(run, 'DungeonRunSlot& slot = it->second;\n    if (verb == "stop")', '\n    if (verb == "status")')
        self.assertNotIn("GetSecurity", stop)
        self.assertNotIn("SEC_", stop)
        self.assertNotIn('"forbidden"', stop)
        # bl32 (pip S3): the only `speed=` GameMaster refusal lives in CheckTestSpeed (runtime/DungeonRunPolicy.h),
        # the glue only forwards its code; the stop surface keeps no gate of its own.
        forbiddens = [m.start() for m in re.finditer('"forbidden"', run)]
        self.assertEqual(forbiddens, [])
        self.assertIn('"forbidden", "Only a GameMaster may start a test-speed run', strip_comments(POLICY))


if __name__ == "__main__":
    unittest.main()
