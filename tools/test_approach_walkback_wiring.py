"""The run leader's approach stand, too-far legs, walk-back target and no-path resume glue.

Gnomeregan r24 slot 4 step 412, Sunken Temple r25 slot 4 step 5, Razorfen Kraul r24
slot 8 steps 299 and 331-343 (2026-10-23). The policy is tested in
tools/test_dungeon_run_policy.cpp; this pins the glue in commands/BotCommands.cpp:
- the approach stand (DungeonRun::ApproachStand): after the tick the glue runs the
  unreachable move's stand-in search once when asked (NearestReachablePoint within
  kApproachStandYd of the step's point) and hands the answer to ApproachStandFound;
- the too-far legs (DungeonRun::TooFarLegWanted): a walk's move refused beyond
  ReactDistance sends a leg along the lead bot's navmesh path (LegOnPath, never a
  shortcut) instead of the pause, and only a refused leg falls back to OrderRefused;
- the walk back (DungeonRun::StartWalkBack) after a regroup is left out beside the
  last pull or boss cleared (LastClearedFight), after the navmesh-hole rule;
- the 'three moves in a row had no walkable path' pause (MoveRefusedUnreachable) sends
  the run back to the first of them and its resume walks back to the last point
  reached (LastReachedBefore in StartWalkBack); no glue of its own.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class ApproachStandWiringTest(unittest.TestCase):
    def test_glue_searches_the_stand_when_asked(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        i = tick.index("if (slot.run.approachStandWanted)")
        self.assertLess(tick.index(": TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"), i)
        self.assertLess(i, tick.index("if (!order.empty())"))
        block = tick[i:tick.index("if (!order.empty())", i)]
        self.assertIn("ApproachStandSearch(lead, slot.run, *step, facts.team, sx, sy, sz);", block)
        self.assertNotIn("NearestReachablePoint", block)
        self.assertIn("TortoiseBots::DungeonRun::ApproachStandFound(slot.run, facts, found, sx, sy, sz);", block)
        self.assertIn("lead->IsAlive() && lead->IsInWorld()", block)

    def test_stand_search_is_a_safe_ring(self):
        # Codex review of f1fdeb67: never the pack's centre, hostiles and the path checked.
        fn = body(COMMANDS, "bool ApproachStandSearch(Player* lead,", "\n}\n")
        self.assertIn("for (float ring = minRing; ring <= kApproachStandYd + 0.01f; ring += 5.0f)", fn)
        self.assertIn("float const minRing = ApproachStandMinRing(step);", fn)
        self.assertIn("HostileCreaturesAround(lead, DistanceTo(lead, step.x, step.y, step.z) + reach, around);", fn)
        self.assertIn("c.los = map->isInLineOfSight(px, py, ground + 2.0f, step.x, step.y, step.z + 2.0f, true);", fn)
        self.assertIn("DistanceTo(lead, px, py, ground), path.Length());", fn)
        self.assertIn("c.path.push_back(PathPoint{v.x, v.y, v.z});", fn)
        self.assertIn("int const best = PickApproachStand(run, step, candidates, hostiles, team);", fn)

    def test_hop_cap_asks_the_stand_before_its_pause(self):
        fn = body(POLICY, "inline void ApproachHop(Run& r, const Facts& f, const PlanStep& s,", "\n}\n")
        cap = fn[fn.index("if (++r.approachHops > kMaxApproachHops)"):]
        self.assertLess(cap.index("if (ApproachStandWanted(r, s))"), cap.index("Pause(r, why);"))
        # The stand's own states come before any hop.
        self.assertLess(fn.index("if (ApproachStand(r, s, dt, f, order, what))"), fn.index("r.hopWalk = true;"))

    def test_resume_keeps_the_stand_and_step_done_clears_it(self):
        resume = body(POLICY, "inline bool Resume(Run& r)", "\n}\n")
        self.assertIn("if (r.approachStandStep == r.step)", resume)
        self.assertIn("r.approachStandResumed = true;", resume)
        reset = body(POLICY, "inline void ResetStep(Run& r)", "\n}\n")
        self.assertNotIn("approachStand", reset)
        self.assertIn("ClearApproachStand(r);", body(POLICY, "inline void NextStep(Run& r, const std::string& note)", "\n}\n"))


class TooFarLegWiringTest(unittest.TestCase):
    def test_refused_too_far_sends_a_leg_before_the_refusal(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        i = tick.index("TortoiseBots::DungeonRun::TooFarLegWanted(it->second.run, order, answer)")
        self.assertLess(tick.index("bool const stallDirectRefused = !accepted &&"), i)
        self.assertLess(i, tick.index("if (!accepted && !stallDirectRefused && !legTaken)"))
        block = tick[i:tick.index("if (!accepted && !stallDirectRefused && !legTaken)")]
        self.assertIn("LegOnPath(lead, run.walkX, run.walkY, run.walkZ, legYd, lx, ly, lz)", block)
        self.assertIn("std::min(TortoiseBots::DungeonRun::kTooFarLegYd, sPlayerbotAIConfig.reactDistance - 10.0f)", block)
        self.assertIn("IssueRunOrder(commander, TortoiseBots::DungeonRun::MoveOrder(lx, ly, lz, false),", block)
        # The run is looked up again after the leg's order (it may release the binding).
        self.assertIn("it = DungeonRuns().find(commanderRaw);\n                if (it == DungeonRuns().end())\n                    return;", block)
        self.assertIn("TooFarLegTaken(it->second.run, facts, lx, ly, lz, legAccepted,", block)
        # Codex review of f1fdeb67: the same leg again is not sent (the refusal pauses, saying why).
        rep = block.index("if (legFound && TortoiseBots::DungeonRun::TooFarLegRepeats(run, lx, ly, lz))")
        self.assertLess(rep, block.index("IssueRunOrder(commander, TortoiseBots::DungeonRun::MoveOrder(lx, ly, lz, false),"))
        self.assertIn("legNote = TortoiseBots::DungeonRun::TooFarLegRepeatNote(run, lx, ly, lz);", block)
        self.assertIn("OrderRefused(it->second.run, order, answer + legNote +", tick)

    def test_leg_follows_the_navmesh_path(self):
        fn = body(COMMANDS, "bool LegOnPath(Player* lead,", "\n}\n")
        self.assertIn("if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH | PATHFIND_SHORTCUT))\n"
                      "        return false;", fn)
        self.assertIn("for (Vector3 const& p : path.getPath())", fn)

    def test_walk_sends_its_point_again_after_the_leg(self):
        fn = body(POLICY, "inline bool WalkTo(Run& r, const Facts& f,", "\n}\n")
        i = fn.index("if (r.legOn && (toLeg <= kArriveYd || (r.legStartYd - toLeg >= kLegProgressYd && r.walkStillMs >= kShortStallMs)))")
        self.assertLess(i, fn.index("if (r.walkTries >= kWalkTries)"))
        fresh = body(fn, "if (fresh)", "return false;")
        self.assertIn("r.legOn = false;", fresh)
        self.assertIn("r.walkLegs = 0;", fresh)


class WalkBackTargetWiringTest(unittest.TestCase):
    def test_last_cleared_fight_after_the_hole_rule(self):
        fn = body(POLICY, "inline void StartWalkBack(Run& r, const Facts& f)", "\n}\n")
        hole = fn.index("std::size_t const hole = FirstUncrossedDirect(r);")
        cleared = fn.index("std::size_t const cleared = LastClearedFight(r, f.team, last);")
        self.assertLess(hole, cleared)
        # The straight target is the last point or a clear one before it (StraightWalkBackTarget, 2026-10-23).
        self.assertLess(cleared, fn.index("std::size_t const target = StraightWalkBackTarget(r, f.team, last, &f);"))
        self.assertLess(cleared, fn.index("r.walkBack = true;\n    r.walkBackDirect = true;\n    r.walkBackIdx = target;"))
        self.assertIn("!r.notCleared.count(i)", body(POLICY, "inline std::size_t LastClearedFight(", "\n}\n"))


class NoPathPauseWiringTest(unittest.TestCase):
    def test_pause_rewinds_and_walk_back_uses_the_reached_point(self):
        fn = body(POLICY, "inline bool MoveRefusedUnreachable(", "\n}\n")
        block = fn[fn.index("if (r.state == State::Running && skips.size() >= kUnreachableSkipsInARow)"):]
        self.assertLess(block.index("r.step = first;"), block.index("Pause(r,"))
        self.assertIn("ForgetMoveRefusals(r);", block)
        self.assertIn("bool const same = r.noPathSkips == skips;", block)
        walk = body(POLICY, "inline void StartWalkBack(Run& r, const Facts& f)", "\n}\n")
        self.assertLess(walk.index("std::size_t const reached = LastReachedBefore(r, r.step);"),
                        walk.index("const PlanStep& p = r.plan->steps[last];"))


class ReviewRoundOneWiringTest(unittest.TestCase):
    """The Opus review of f1fdeb67."""

    def test_event_rewinds_forget_the_route_memory(self):
        for fn in ("inline void EventFailed(", "inline bool RearmEventOnWipe("):
            code = body(POLICY, fn, "\n}\n")
            self.assertLess(code.index("ForgetMoveRefusals(r);"), code.index("ForgetRewoundRoute(r);"))
        # A resume keeps both (it is no rewind).
        self.assertNotIn("ForgetRewoundRoute", body(POLICY, "inline bool Resume(Run& r)", "\n}\n"))

    def test_leg_needs_a_full_path_and_a_clear_end(self):
        fn = body(COMMANDS, "bool LegOnPath(Player* lead,", "\n}\n")
        self.assertIn("PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH | PATHFIND_SHORTCUT", fn)
        self.assertIn("HostileCreaturesAround(lead, maxYd + TortoiseBots::DungeonRun::kHopPullYd, around);", fn)
        self.assertIn("return TortoiseBots::DungeonRun::PickSafeLeg(points, maxYd, hostiles, ox, oy, oz);", fn)

    def test_stand_walk_is_a_hop_and_spent_on_sight(self):
        stand = body(POLICY, "inline bool ApproachStand(Run& r, const PlanStep& s,", "\n}\n")
        self.assertIn("r.hopWalk = true;\n        r.hopX = r.approachStandX;", stand)
        fight = body(POLICY, "inline void FightStep(Run& r, const Facts& f, const PlanStep& s,", "\n}\n")
        self.assertLess(fight.index("if (r.approachStandStep == r.step && r.approachStand == 4)\n    {\n        r.approachStand = 3;\n"
                                    "        r.approachHops = 0;\n        r.approachBestYd = 0.0f;"),
                        fight.index("if (FinishHopInSight("))


if __name__ == "__main__":
    unittest.main()
