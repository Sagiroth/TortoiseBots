"""Travel leg chaining: the next leg dispatches in the same turn when the
current leg's spline is nearly finished.

Pins the wiring, not the math (the policy itself is tested in
tools/test_travel_leg_chain_policy.cpp):
- DispatchMovement wakes the next turn a margin BEFORE a spline leg ends
  (TRAVEL_LEG_CHAIN_REMAIN_MS), instead of after the leg time capped at
  MaxWaitForMove; a plain MovePoint leg keeps its own wait.
- The travel action stays useful at the spline tail (TravelLegChainDue) so
  Execute dispatches the next leg in the same turn; pure walk legs only,
  and a failed move (or a path with fewer than 2 points) never chains.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRAVEL = (ROOT / "ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp").read_text()
MOVEMENT = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/strategy/actions/MoveToTravelTargetAction.h").read_text()


class LegChainDispatchWiringTest(unittest.TestCase):
    def test_spline_leg_wakes_before_it_ends(self):
        fn = MOVEMENT[MOVEMENT.index("bool MovementAction::DispatchMovement("):]
        fn = fn[:fn.index("Unit* MovementAction::GetMover(")]
        self.assertIn("TRAVEL_LEG_CHAIN_REMAIN_MS", fn)
        self.assertIn("chainWait", fn)
        self.assertIn("WaitForReach(chainWait)", fn)

    def test_plain_movepoint_leg_keeps_its_own_wait(self):
        fn = MOVEMENT[MOVEMENT.index("bool MovementAction::DispatchMovement("):]
        fn = fn[:fn.index("Unit* MovementAction::GetMover(")]
        self.assertIn("else\n        WaitForReach(size);", fn)


class LegChainTravelWiringTest(unittest.TestCase):
    def test_tail_check_runs_before_the_moving_gate(self):
        fn = TRAVEL[TRAVEL.index("bool MoveToTravelTargetAction::isUseful()"):]
        tail = fn.index("TravelLegChainDue(ai, bot)")
        self.assertLess(tail, fn.index("LastChangeDelay() < 10"))

    def test_chains_only_while_moving(self):
        fn = TRAVEL[TRAVEL.index("bool MoveToTravelTargetAction::isUseful()"):]
        self.assertIn("bot->IsMoving() && TravelLegChainDue(ai, bot)", fn)

    def test_pure_walk_only(self):
        helper = TRAVEL[TRAVEL.index("bool MoveToTravelTargetAction::TravelLegChainDue("):]
        helper = helper[:helper.index("bool MoveToTravelTargetAction::TravelMoveMadeNoProgress()")]
        for guard in ("IsInCombat()", "IsTaxiFlying()", "IsFlying()", "IsMounted()",
                      "IsBeingTeleported()", "GetTransport()", "InBattleGround()",
                      "IsInWater()", "IsUnderwater()", "MOVEFLAG_SWIMMING",
                      "IsDungeon()", "BOT_STATE_COMBAT"):
            self.assertIn(guard, helper)

    def test_failure_handling_untouched(self):
        helper = TRAVEL[TRAVEL.index("bool MoveToTravelTargetAction::TravelLegChainDue("):]
        helper = helper[:helper.index("bool MoveToTravelTargetAction::TravelMoveMadeNoProgress()")]
        self.assertIn("moveFailReason != MOVE_FAIL_NONE", helper)
        self.assertIn("GetPath().size() < 2", helper)
        self.assertIn("IsWalkable()", helper)

    def test_own_spline_only(self):
        helper = TRAVEL[TRAVEL.index("bool MoveToTravelTargetAction::TravelLegChainDue("):]
        helper = helper[:helper.index("bool MoveToTravelTargetAction::TravelMoveMadeNoProgress()")]
        self.assertIn('"DispatchMovement"', helper)
        self.assertIn("TravelLegChainDecide", helper)

    def test_helper_declared(self):
        self.assertIn("static bool TravelLegChainDue(PlayerbotAI* ai, Player* bot);", HEADER)


if __name__ == "__main__":
    unittest.main()
