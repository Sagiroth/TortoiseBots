"""The stay walk and its trigger share one arrival rule and one back-off.

Crescent Grove r26 step 272 (2026-10-23): a tauren lead bot stood 5.67 yd from its
point on the ground and 6.6 yd below it. The walk's isUseful measured the ground
distance minus the bounding radius (4.7 yd: USELESS, every `cmd move` refused), the
trigger 3D minus the radius (active every tick) and the run leader's deadband the raw
ground distance (not arrived): the run paused four times. The walk and its trigger now
ask one function, ai::StayWalkDueNow (PositionAction.cpp): TortoiseBots::StayPointReached
(runtime/StayArrivalPolicy.h, raw 3D distance) on the stay point's map, and a back-off
after failed walks, judged by where a walk takes the bot (not by its dispatch). An
explicit order clears the back-off. The run leader's deadband was removed (on the 3D rule
its stalled arrival covers it). The other MoveToPositionAction qualifiers (guard, return,
pull) keep their own rule.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ACTIONS = ROOT / "ai/playerbot/strategy/actions"
POSITION = (ACTIONS / "PositionAction.cpp").read_text()
POSITION_H = (ACTIONS / "PositionAction.h").read_text()
GO = (ACTIONS / "GoAction.cpp").read_text()
SHORTCUT = (ACTIONS / "ChatShortcutActions.cpp").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/GenericTriggers.cpp").read_text()
RUN = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
POLICY = (ROOT / "runtime/StayArrivalPolicy.h").read_text()
LAST_MOVE = (ROOT / "ai/playerbot/strategy/values/LastMovementValue.h").read_text()
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
CONTEXT = (ROOT / "commands/BotCommandContext.cpp").read_text()

ORDERED = "TortoiseBots::StayWalkOrdered("


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class StayArrivalWiringTest(unittest.TestCase):
    def test_rule_is_raw_3d(self):
        self.assertIn("return std::sqrt(dx * dx + dy * dy + dz * dz);", body(POLICY, "inline float StayPointDistance("))
        self.assertIn("return StayPointDistance(dx, dy, dz) <= range;", body(POLICY, "inline bool StayPointReached("))

    def test_one_question_for_walk_and_trigger(self):
        self.assertIn('#include "../../../../runtime/StayArrivalPolicy.h"', POSITION)
        self.assertIn("bool StayWalkDueNow(PlayerbotAI* ai, Player* bot, PositionEntry const& stayPosition);", POSITION_H)
        fn = body(POSITION, "bool ai::StayWalkDueNow(PlayerbotAI* ai, Player* bot, PositionEntry const& stayPosition)")
        self.assertIn('->GetValue<LastMovement&>("last movement")->Get().stayWalk;', fn)
        self.assertIn("TortoiseBots::StayWalkTrack(backoff, stayPosition.x, stayPosition.y, stayPosition.z, stayPosition.mapId);", fn)
        for axis in "XYZ":
            self.assertIn(f"float const d{axis.lower()} = bot->GetPosition{axis}() - stayPosition.{axis.lower()};", fn)
        self.assertIn("bool const moving = sServerFacade.isMoving(bot) || !bot->movespline->Finalized();", fn)
        self.assertIn('return TortoiseBots::StayWalkDue(backoff, TortoiseBots::StayPointReached(dx, dy, dz, ai->GetRange("follow")),\n'
                      "        TortoiseBots::StayPointDistance(dx, dy, dz), moving, bot->IsInCombat(), bot->GetTransport() != nullptr, WorldTimer::getMSTime());", fn)

    def test_stay_walk_is_useful_by_the_shared_question(self):
        cls = body(POSITION_H, "class ReturnToStayPositionAction : public MoveToPositionAction", "\n    };\n")
        self.assertIn("virtual bool isUseful() override;", cls)
        self.assertIn("virtual bool Execute(Event& event) override;", cls)
        fn = body(POSITION, "bool ReturnToStayPositionAction::isUseful()")
        self.assertIn('PositionEntry stayPosition = AI_VALUE(PositionMap&, "position")["stay"];', fn)
        # The map check the old "distance" value made (DistanceValue.h: 0 on another map).
        self.assertIn("if (!stayPosition.isSet() || stayPosition.mapId != bot->GetMapId() || !ai->CanMove())\n        return false;", fn)
        self.assertIn("return StayWalkDueNow(ai, bot, stayPosition);", fn)
        self.assertNotIn('"distance"', fn)

    def test_stay_walk_feeds_its_dispatch(self):
        fn = body(POSITION, "bool ReturnToStayPositionAction::Execute(Event& event)")
        self.assertIn("bool const moved = MoveToPositionAction::Execute(event);", fn)
        self.assertIn("TortoiseBots::StayWalkTrack(backoff, stayPosition.x, stayPosition.y, stayPosition.z, stayPosition.mapId);", fn)
        self.assertIn("TortoiseBots::StayWalkDispatched(backoff, moved, TortoiseBots::StayPointDistance(bot->GetPositionX() - stayPosition.x,\n"
                      "        bot->GetPositionY() - stayPosition.y, bot->GetPositionZ() - stayPosition.z), bot->IsInCombat(),\n"
                      "        bot->GetTransport() != nullptr, WorldTimer::getMSTime());", fn)
        self.assertLess(fn.index("MoveToPositionAction::Execute(event)"), fn.index("StayWalkDispatched("))
        self.assertIn("return moved;", fn)
        self.assertNotIn("StayWalkSucceeded", fn)  # a dispatch is not a success

    def test_backoff_lives_with_the_movement_state(self):
        self.assertIn('#include "../../../../runtime/StayArrivalPolicy.h"', LAST_MOVE)
        self.assertIn("TortoiseBots::StayWalkBackoff stayWalk;", LAST_MOVE)
        self.assertIn("stayWalk = other.stayWalk;", LAST_MOVE)
        self.assertNotIn("stayWalk", body(LAST_MOVE, "void clear()", "\n        }\n"))
        self.assertEqual(LAST_MOVE.count("stayWalk"), 3)  # the field and its copy line: a move's clear() keeps it

    def test_explicit_orders_clear_the_backoff(self):
        anchor = body(COMMANDS, "void SetStayAnchor(")
        self.assertIn(ORDERED + 'ai->GetAiObjectContext()->GetValue<ai::LastMovement&>("last movement")->Get().stayWalk);', anchor)
        self.assertLess(anchor.index('posMap["stay"] = stayPos;'), anchor.index(ORDERED))
        go = body(GO, "void GoAction::UpdateStrategyPosition(", "\n   else if")
        self.assertIn(ORDERED + 'AI_VALUE(LastMovement&, "last movement").stayWalk);', go)
        stay = body(SHORTCUT, "bool StayChatShortcutAction::Execute(Event& event)")
        self.assertIn('SetPosition(bot, "stay");\n    // An explicit order is a fresh stay walk (StayWalkOrdered).\n    ' + ORDERED, stay)
        hold = body(CONTEXT, "        ai::PositionEntry stayPos = posMap[\"stay\"];", 'posMap["pull hold"] = holdPos;')
        self.assertIn(ORDERED + 'ai->GetAiObjectContext()->GetValue<ai::LastMovement&>("last movement")->Get().stayWalk);', hold)
        callers = sorted(p.name for p in list((ROOT / "ai").rglob("*.cpp")) + list((ROOT / "commands").rglob("*.cpp"))
                         if ORDERED in p.read_text(errors="replace"))
        self.assertEqual(callers, ["BotCommandContext.cpp", "ChatShortcutActions.cpp", "GoAction.cpp", "Party.cpp"])

    def test_other_qualifiers_keep_their_rule(self):
        fn = body(POSITION, "bool MoveToPositionAction::isUseful()")
        self.assertIn('AI_VALUE2(float, "distance", std::string("position_") + qualifier)', fn)
        self.assertNotIn("StayPointReached", fn)

    def test_trigger_asks_the_walks_question(self):
        self.assertIn('#include "playerbot/strategy/actions/PositionAction.h"', TRIGGERS)
        fn = body(TRIGGERS, "bool ReturnToStayPositionTrigger::IsActive()")
        self.assertIn("if (stayPosition.isSet() && stayPosition.mapId == bot->GetMapId())", fn)
        self.assertIn("return StayWalkDueNow(ai, bot, stayPosition);", fn)
        self.assertNotIn("GetDistance(", fn)

    def test_run_leader_sees_a_resting_walk(self):
        fn = body(COMMANDS, "void ReadRunFacts(DungeonRunSlot const& slot, Player* lead,")
        self.assertIn("f.leadStayWalkResting = TortoiseBots::StayWalkBackingOff(lastMove.stayWalk, WorldTimer::getMSTime());", fn)
        self.assertIn("f.leadStayWalkFails = lastMove.stayWalk.fails;", fn)
        self.assertIn("f.leadMoveFailed = ai::PositionMoveFailing(lastMove.positionMoveFailMs, WorldTimer::getMSTime()) || f.leadStayWalkResting;", fn)

    def test_run_leader_has_no_separate_deadband(self):
        self.assertNotIn("InFollowDeadband", RUN.replace("separate deadband rule, InFollowDeadband, was removed", ""))
        self.assertNotIn("plainWalk", RUN)
        self.assertIn("static_assert(kStalledArriveYd >= 4.0f,", RUN)

    def test_only_these_callers(self):
        callers = []
        for path in list((ROOT / "ai").rglob("*.cpp")) + list((ROOT / "runtime").rglob("*.h")) + \
                list((ROOT / "commands").rglob("*.cpp")):
            if path.name == "StayArrivalPolicy.h":
                continue
            n = len(re.findall(r"\bStayPointReached\(|\bStayWalkDue\(", path.read_text(errors="replace")))
            if n:
                callers.append((path.name, n))
        self.assertEqual(sorted(callers), [("PositionAction.cpp", 2)])


if __name__ == "__main__":
    unittest.main()
