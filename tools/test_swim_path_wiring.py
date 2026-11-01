"""The core's straight swim answer reaches the bot's own move (SwimPathPolicy.h).

Blackfathom Deeps 2026-10-22/23: a lead bot standing in a deep pool got the
core's straight swim path (NORMAL|UNDERWATER 0x81, NORMAL|NOT_USING_PATH 0x11)
from the PathFinder. WorldPosition::getPathStepFrom kept only type ==
PATHFIND_NORMAL (or INCOMPLETE), threw that answer away, and the move failed
(dispatch-short) every 2 s, while `cmd move` had acked it. The pure rule and
the water test are tested in tools/test_swim_path_policy.cpp; this pins the
glue:
- getPathStepFrom decides by ClassifyPathStepType / KeepPathStep, never by an
  equality test on PATHFIND_NORMAL, and asks the water and sight test
  (SwimPathFits) only for a swim answer;
- SwimPathFits samples every leg in water or ocean only (no magma, no slime),
  asks the line of sight with dynamic objects, and refuses a bot on a
  transport or on another map;
- the policy's bits are static_asserted against the core's.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORLDPOS = (ROOT / "ai/playerbot/WorldPosition.cpp").read_text()
MOVES = (ROOT / "ai/playerbot/strategy/actions/MovementActions.cpp").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class SwimPathWiringTest(unittest.TestCase):
    def setUp(self):
        self.step = body(WORLDPOS, "std::vector<WorldPosition> WorldPosition::getPathStepFrom(const WorldPosition& startPos, std::unique_ptr<PathFinder>& pathfinder",
                         "\n}\n")
        self.fits = body(WORLDPOS, "bool SwimPathFits(", "\n    }\n}\n")

    def test_step_keeps_by_the_policy(self):
        self.assertIn("ClassifyPathStepType(static_cast<uint32>(type), forceNormalPath)", self.step)
        self.assertIn("verdict == PathStepVerdict::KeepIfSwimLine && SwimPathFits(retvec, bot)", self.step)
        self.assertIn("if (KeepPathStep(static_cast<uint32>(type), forceNormalPath, swimLine, swimLine))\n        return retvec;", self.step)
        # The old equality test is gone from the final decision.
        self.assertNotIn("|| type == PATHFIND_NORMAL)", self.step)

    def test_step_keeps_the_underwater_incomplete_branch(self):
        # Codex review 2026-10-23: the partial path's swim-on step asks the full-line test.
        self.assertIn("if (type == PATHFIND_INCOMPLETE && !retvec.empty())", self.step)
        self.assertIn("if (dist < 50.0f && SwimPathFits({ lastPoint, end }, bot))", self.step)
        self.assertNotIn("isUnderWater()", self.step)
        self.assertNotIn("IsInLineOfSight(end)", self.step)

    def test_fits_samples_water_only_and_asks_sight(self):
        # Blackfathom Deeps r26-19z44e: the request goes to the core as the policy's
        # kSwimLiquidRequest (all liquids; a water | ocean request found no BFD water,
        # the vmap ANDs it with the WMO's raw liquid id 1), and the answer's type
        # flags (magma, slime) are handed back for the policy to refuse.
        self.assertIn("terrain->getLiquidStatus(x, y, z, static_cast<uint8>(request), &data)", self.fits)
        self.assertIn("typeFlags = status != LIQUID_MAP_NO_WATER ? data.type_flags : 0;", self.fits)
        self.assertNotIn("MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN", self.fits)
        self.assertIn("ai::kSwimLiquidRequest == 0,", WORLDPOS)
        self.assertIn("ai::SwimLineInWater(a.getX(), a.getY(), a.getZ(), b.getX(), b.getY(), b.getZ(), liquidAt)", self.fits)
        self.assertIn("            if (!map->isInLineOfSight(a.getX(), a.getY(), a.getZ() + 0.5f, b.getX(), b.getY(), b.getZ() + 0.5f, true))\n                return false;", self.fits)
        self.assertIn("            if (!ai::SwimLineInWater(", self.fits)
        # Every leg, not just the first.
        self.assertIn("for (size_t i = 1; i < path.size(); ++i)", self.fits)

    def test_fits_refuses_transport_and_other_map(self):
        self.assertIn("if (!bot || bot->GetTransport() || path.size() < 2)\n            return false;", self.fits)
        self.assertIn("a.GetMapId() != map->GetId() || b.GetMapId() != map->GetId()", self.fits)

    def test_bits_are_asserted_against_the_core(self):
        for name in ("NORMAL", "SHORTCUT", "INCOMPLETE", "NOPATH", "NOT_USING_PATH", "DEST_FORCED", "FLYPATH", "UNDERWATER"):
            self.assertRegex(WORLDPOS, r"ai::kPathType\w+ == PATHFIND_%s\b" % name)
        for name in ("NO_WATER", "ABOVE_WATER", "WATER_WALK", "IN_WATER", "UNDER_WATER"):
            self.assertRegex(WORLDPOS, r"ai::kLiquid\w+ == LIQUID_MAP_%s\b" % name)

    def test_no_other_bot_move_path_tests_type_by_equality(self):
        # getPathStepFrom was the bot's route filter; the module's own move code
        # must not grow a second equality filter on the route.
        self.assertNotRegex(MOVES, r"getPathType\(\)\s*==\s*PATHFIND_NORMAL\s*\)\s*\n\s*return")


POLICY = (ROOT / "ai/playerbot/SwimPathPolicy.h").read_text()
LASTMOVE = (ROOT / "ai/playerbot/strategy/values/LastMovementValue.h").read_text()


class SwimHandoffWiringTest(unittest.TestCase):
    def setUp(self):
        self.move2 = body(MOVES, "bool MovementAction::MoveTo2(const WorldPosition& endPos,", "\n}\n")
        self.hand = body(MOVES, "MovementAction::SwimHandoffResult MovementAction::SwimHandoff(", "\n}\n")

    def test_moveto2_asks_after_the_route_and_in_the_collapse(self):
        resolve = self.move2.index("TravelPath movePath = ResolveMovePath(startPos, endPos, mover, lastMove);")
        first = self.move2.index("switch (SwimHandoff(startPos, endPos, movePath, lastMove, packHold, ignoreEnemyTargets))")
        empty = self.move2.index("lastMove.moveFailReason = MOVE_FAIL_EMPTY_ROUTE;")
        self.assertLess(resolve, first)
        self.assertLess(first, empty)
        second = self.move2.index("switch (SwimHandoff(startPos, endPos, movePath, lastMove, packHold, ignoreEnemyTargets))", first + 1)
        self.assertLess(self.move2.index("movePath.makeShortCut(startPos, sPlayerbotAIConfig.reactDistance, bot);"), second)
        # Collapsed route with no swim counts as a failure (MoveFailReason
        # shortcut-empty), so the travel budget fills and the drop/rescue
        # machinery runs; a swim handoff still reports Moving.
        self.assertLess(second, self.move2.index("lastMove.moveFailReason = MOVE_FAIL_SHORTCUT_EMPTY;"))
        self.assertLess(second, self.move2.index("movePath.ClipPath(ai, mover, ignoreEnemyTargets, packHold);"))

    def test_handoff_guards_and_dispatch(self):
        h = self.hand
        self.assertIn("ai->IsRealPlayer() || bot->GetTransport() || bot->IsFlying() || bot->IsTaxiFlying()", h)
        self.assertIn("bot->IsInWater() || bot->IsUnderwater() || bot->HasMovementFlag(MOVEFLAG_SWIMMING)", h)
        # Round 1 (a): a follow that holds before packs never hands off.
        self.assertIn("if (packHold != nullptr)", h)
        self.assertLess(h.index("if (packHold != nullptr)"), h.index("PathFinder path(bot);"))
        # Round 1: a 1-point route far away is progress, not a stall.
        self.assertIn("f.routeEndFromBot = route.empty() ? 0.0f : route.back().distance(startPos);", h)
        self.assertNotIn("route.size() < 2 ? 0.0f", h)
        self.assertLess(h.index("f.routeEndFromBot > ai::kSwimNoProgressYd"), h.index("PathFinder path(bot);"))
        self.assertIn("map->FindCollisionModel(", h)
        self.assertIn("terrain->IsSwimmable(startPos.getX(), startPos.getY(), startPos.getZ(), 1.5f, &here)", h)
        self.assertIn("GridMapLiquidData endData{};", h)
        self.assertIn("ai::kSwimLiquidRequest, &endData);", h)
        self.assertIn("f.endInWater = ai::SwimSampleInWater(", h)
        self.assertIn("coreEnd.z + 0.5f", h)
        self.assertIn("bool const swimShortcut =", h)
        self.assertIn("if (kind == ai::SwimHandoff::SwimOut || swimShortcut)", h)
        self.assertIn("std::string const why = kind == ai::SwimHandoff::CoreLine && swimShortcut", h)

    def test_handoff_asks_the_policy_for_kind(self):
        # The glue feeds every filled fact through the policy: pin the call
        # statement feeding `kind` (a declaration grep would miss a deletion).
        h = self.hand
        self.assertIn("ai::SwimHandoff const kind = ai::ChooseSwimHandoff(f);", h)
        self.assertLess(h.index("f.surfaceLineClear ="), h.index("ai::SwimHandoff const kind = ai::ChooseSwimHandoff(f);"))
        self.assertLess(h.index("ai::SwimHandoff const kind = ai::ChooseSwimHandoff(f);"), h.index("if (kind == ai::SwimHandoff::None)"))

    def test_handoff_round1_clip_magma_slime_ground(self):
        h = self.hand
        # Round 1 (b)/(2): the 0x1 answer walks its own points (path.getPath()) through the same
        # ClipPath (hostile/hazard + packHold), and launches exactly that validated spline.
        self.assertIn("for (G3D::Vector3 const& p : path.getPath())", h)
        self.assertIn("clipped.ClipPath(ai, bot, ignoreEnemyTargets, packHold);", h)
        self.assertIn('Movement::MoveSplineInit init(*bot, "SwimHandoffCore");', h)
        # The 0x1 branch launches the validated spline only (no MovePoint recompute beside the 0x81 line).
        core = h[h.index('if (kind == ai::SwimHandoff::CoreLine && f.coreType == ai::kPathTypeNormal)'):h.index('else if (kind == ai::SwimHandoff::CoreLine)')]
        self.assertIn('Movement::MoveSplineInit init(*bot, "SwimHandoffCore");', core)
        # Round 1 (c): the 0x81 line and the swim-out legs run the same ClipPath, segment by
        # segment; a cut refuses. packHold is threaded (a hold clips too).
        self.assertIn("static bool SwimHandoffLegClear(", MOVES)
        self.assertIn("clipped.addPath({ leg[i - 1], leg[i] });", MOVES)
        self.assertIn("clipped.ClipPath(ai, mover, ignoreEnemyTargets, packHold);", MOVES)
        self.assertIn("if (!SwimHandoffLegClear(ai, bot, ignoreEnemyTargets, packHold, linePoints))", h)
        # Round 1 (2): the walked path (not the chord) feeds the sampled guard.
        # Round 2: ONE sampled liquid-hazard guard for magma AND slime (the old
        # waypoint-only slime loop missed a leg crossing a pool between two safe
        # points); the hand-off passes slime on, ordinary moves pass it off.
        self.assertIn("RouteEntersLiquidHazard(bot, startPos, line, hazardAt, true, hazardIsSlime)", h)
        self.assertLess(h.index("for (G3D::Vector3 const& p : path.getPath())"), h.index("RouteEntersLiquidHazard(bot, startPos, line, hazardAt, true,"))
        self.assertIn("hazardIsSlime", h)
        self.assertNotIn("slimeOnLine", h)
        self.assertIn("linePoints = coreRoute;", h)
        self.assertIn("coreRoute = kept;", h)
        self.assertIn('"slime"', h)
        self.assertIn('"magma"', h)
        # Round 1 (3): the swim-out end needs ground within 1 yd below it.
        self.assertIn("map->GetHeight(endPos.getX(), endPos.getY(), endPos.getZ() + 0.5f)", h)
        self.assertIn("INVALID_HEIGHT", h)
        self.assertIn("f.endHasGround", h)
        # Round 1: every refused stall logs its reason, rate-limited on the hand-off stamp.
        self.assertIn("swim hand-off refused (%s: %s)", MOVES)
        self.assertIn("static void LogSwimHandoffRefused(", MOVES)
        for reason in ('"core path", "clipped"', '? "core line" : "swim out", "clipped"',
            '? "core line" : "swim out", "slime"', '? "core line" : "swim out", "magma"',
            '? "core line" : "swim out", "route too long to check"', '"model on the line"', '"over 100 yd"'):
            self.assertIn(reason, h)
        refused = body(MOVES, "static void LogSwimHandoffRefused(", "\n}\n")
        self.assertIn("lastMove.swimHandoffLogAt = now;", refused)
        # The core's first point is the bot's own spot: kept once, never spliced into a zero leg.
        self.assertIn("coreRoute.insert(coreRoute.begin(), startPos);", h)

    def test_policy_takes_0x11_only_in_water(self):
        choose = body(POLICY, "inline SwimHandoff ChooseSwimHandoff(SwimHandoffFacts const& f)", "\n    }\n")
        self.assertIn("f.coreType == (kPathTypeNormal | kPathTypeUnderwater)", choose)
        self.assertIn("kPathTypeNotUsingPath", choose)
        self.assertIn("f.swimmableHere", choose)
        self.assertIn("f.endInWater", choose)
        inwater = body(POLICY, "inline bool SwimLineInWater(", "\n    }\n")
        self.assertIn("return SwimLineFirstFailing(", inwater)

    def test_log_stamp_survives_a_clear(self):
        self.assertIn("swimHandoffLogAt = other.swimHandoffLogAt;", LASTMOVE)
        clear = body(LASTMOVE, "void clear()", "\n        }\n")
        self.assertNotIn("swimHandoffLogAt", clear)

    def test_why_mirrors_the_fits_test(self):
        why = body(WORLDPOS, "std::string WorldPosition::SwimLineWhy(", "\n}\n")
        self.assertIn("ai::SwimLineFirstFailing(", why)
        self.assertIn("map->isInLineOfSight(a.getX(), a.getY(), a.getZ() + 0.5f, b.getX(), b.getY(), b.getZ() + 0.5f, true)", why)


if __name__ == "__main__":
    unittest.main()
