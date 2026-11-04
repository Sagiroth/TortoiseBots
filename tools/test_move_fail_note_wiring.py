"""A failed move says why, and the run leader's stuck note names the lead bot's water state.

Blackfathom Deeps 2026-10-23: the lead bot's "move to position" FAILED 172
times in a row from a deep pool, the action log said nothing more, and the
run leader paused with "the lead bot is stuck" only. The pure parts are tested
in tools/test_move_fail_note_policy.cpp and tools/test_dungeon_run_policy.cpp
(TestStuckPauseNamesTheLeadsWaterState); this pins the glue:
- the engine's FAILED line appends Action::GetFailNote(); MovementAction's
  note is the fail reason, the water state and the route's path type;
- getPathStepFrom records the path type (WorldPosition::LastPathStepType) and
  MoveTo2 resets it before its route and keeps it after;
- MoveToPositionAction::Execute stamps a failed move's time;
- CommanderMove hands the run leader the path type its check saw, the run
  loop notes it for an acked plain move, and ReadRunFacts reads the lead
  bot's water state and failing move.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = (ROOT / "ai/playerbot/strategy/Engine.cpp").read_text()
ACTION = (ROOT / "ai/playerbot/strategy/Action.h").read_text()
MOVES = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
POSITION = (ROOT / "ai/playerbot/strategy/actions/PositionAction.cpp").read_text()
WORLDPOS = (ROOT / "ai/playerbot/WorldPosition.cpp").read_text()
LASTMOVE = (ROOT / "ai/playerbot/strategy/values/LastMovementValue.h").read_text()
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class MoveFailNoteWiringTest(unittest.TestCase):
    def test_failed_line_carries_the_note(self):
        self.assertIn('LogAction("A:%s - FAILED src=%s base=%.3f eff=%.3f%s", action->getName().c_str(), event.getSource().c_str(), oldRelevance, relevance,\n'
                      '                                action->GetFailNote().c_str());', ENGINE)
        self.assertIn("virtual std::string GetFailNote() { return std::string(); }", ACTION)

    def test_movement_note_reads_reason_water_and_path(self):
        fn = body(MOVES, "std::string MovementAction::GetFailNote()", "\n}\n")
        self.assertIn("bool const tried = lastMove.moveTries != failNoteMark;", fn)
        self.assertIn("MoveFailNote(MoveFailNoteReason(failNoteMark, lastMove.moveTries, MoveFailReasonName(lastMove.moveFailReason)),", fn)
        self.assertIn("bot->HasMovementFlag(MOVEFLAG_SWIMMING), tried ? lastMove.movePathType : 0);", fn)

    def test_reason_belongs_to_this_try(self):
        # Opus review 2026-10-23: the mark is taken before Execute and each try resets the reason.
        self.assertIn("                        action->BeforeExecute();\n                        actionExecuted = ListenAndExecute(action, event);", ENGINE)
        self.assertIn("failNoteMark = AI_VALUE(LastMovement&, \"last movement\").moveTries;", body(MOVES, "void MovementAction::BeforeExecute()", "\n}\n"))
        move = body(MOVES, "bool MovementAction::MoveTo2(", "\n}\n")
        self.assertIn("    ++lastMove.moveTries;\n    lastMove.moveFailReason = MOVE_FAIL_NONE;\n    lastMove.movePathType = 0;\n\n    if (!endPos.isValid())", move)
        unit = body(MOVES, "bool MovementAction::MoveTo(Unit* target, float distance)", "\n}\n")
        self.assertIn("    ++unitMove.moveTries;\n    unitMove.moveFailReason = MOVE_FAIL_NONE;", unit)
        self.assertIn("moveTries = other.moveTries;", LASTMOVE)
        self.assertNotIn("moveTries = 0;\n", body(LASTMOVE, "void clear()", "\n        }\n"))
        self.assertIn("virtual void BeforeExecute() {}", ACTION)

    def test_path_type_is_recorded_around_the_route(self):
        step = body(WORLDPOS, "std::vector<WorldPosition> WorldPosition::getPathStepFrom(const WorldPosition& startPos, std::unique_ptr<PathFinder>& pathfinder", "\n}\n")
        self.assertIn("type = pathfinder->getPathType();\n    LastPathStepType() = static_cast<uint32>(type);", step)
        move = body(MOVES, "bool MovementAction::MoveTo2(", "\n}\n")
        self.assertIn("    WorldPosition::LastPathStepType() = 0;\n    TravelPath movePath = ResolveMovePath(startPos, endPos, mover, lastMove);\n"
                      "    lastMove.movePathType = WorldPosition::LastPathStepType();", move)

    def test_last_movement_keeps_and_clears_the_new_fields(self):
        for field in ("movePathType", "positionMoveFailMs"):
            self.assertIn("%s = other.%s;" % (field, field), LASTMOVE)
            self.assertIn("            %s = 0;\n" % field, body(LASTMOVE, "void clear()", "\n        }\n"))

    def test_position_move_stamps_a_failure(self):
        fn = body(POSITION, "bool MoveToPositionAction::Execute(", "\n}\n")
        self.assertIn("bool const moved = MoveTo(bot->GetMapId(), pos.x, pos.y, pos.z, idle);", fn)
        self.assertIn('AI_VALUE(LastMovement&, "last movement").positionMoveFailMs = moved ? 0 : (WorldTimer::getMSTime() | 1u);', fn)
        self.assertIn("return moved;", fn)

    def test_commander_move_hands_over_its_path_type(self):
        # The path type is read in the shared check (CheckLeadMove) and captured by
        # `cmd move` only when it asked for a path (not "hold here").
        check = body(COMMANDS, "static LeadMoveCheck CheckLeadMove(", "\n}\n\nstatic bool CommanderMove(")
        self.assertIn("check.rawPathType = static_cast<uint32>(path.getPathType());", check)
        self.assertIn("check.facts.pathType = check.rawPathType;", check)
        fn = body(COMMANDS, "static bool CommanderMove(", "\n}\n")
        self.assertIn("if (s_runOrderCapture.active && !RemoteCommander::IsAtLead(facts.distanceFromLead))\n    {\n"
                      "        s_runOrderCapture.movePathRead = true;\n        s_runOrderCapture.movePathType = facts.pathType;", fn)
        issue = body(COMMANDS, "bool IssueRunOrder(", "\n}\n")
        self.assertIn("if (captured)\n        *captured = got;", issue)
        self.assertIn("TortoiseBots::DungeonRun::IsGatherOrder(it->second.run, order), &captured);", COMMANDS)
        self.assertIn("if (accepted && captured.movePathRead)\n            TortoiseBots::DungeonRun::NoteMovePathType(it->second.run, order, captured.movePathType);", COMMANDS)

    def test_run_facts_read_the_lead_bots_water_and_move(self):
        fn = body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,", "\n}\n")
        self.assertIn("f.leadInWater = lead->IsInWater() || lead->IsUnderwater();", fn)
        self.assertIn("f.leadMoveFailed = ai::PositionMoveFailing(lastMove.positionMoveFailMs, WorldTimer::getMSTime()) || f.leadStayWalkResting;", fn)
        self.assertIn("f.leadMoveFailReason = ai::MoveFailReasonName(lastMove.moveFailReason);", fn)

    def test_stuck_pause_carries_the_note(self):
        fn = body(POLICY, "inline bool WalkTo(Run& r, const Facts& f,", "\n}\n")
        self.assertIn('Pause(r, "the lead bot is stuck " + Num(dist) + " yd from " + Num(x) + " " + Num(y) + " " + Num(z) +\n                StuckNote(r, f));', fn)


if __name__ == "__main__":
    unittest.main()
