"""The run leader's time-sink fixes of 2026-10-23 reach the game (tools/test_run_time_sinks_policy.cpp tests the
policy itself; this file checks the glue in commands/BotCommands.cpp, the config plumbing and the docs that carry it).

(A) the walk back: the glue sets Run::startStep next to Start (a placed run never walks back before it), passes the
    pre-start steps as uncleared to the straight path's pack check (UnclearedForPath), and reports a partial approach
    path (Facts::approachIncomplete) for a straight walk back; the policy rescues a stuck walk back instead of pausing,
    and the retreat at two dead is read from AiPlayerbot.DungeonRunRetreatAt2Dead on every tick.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = "\n".join((ROOT / "commands" / n).read_text() for n in ("Commander.cpp", "Action.cpp", "Party.cpp", "BotCommands.cpp"))
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class TimeSinksWiringTest(unittest.TestCase):
    def test_start_step_is_set_next_to_start(self):
        self.assertIn("        slot.run = Start(plan, startStep);\n"
                      "        slot.run.startStep = startStep;", COMMANDS)
        floor = body(POLICY, "inline std::size_t ReplayFloor(const Run& r)", "\n}\n")
        self.assertIn("r.startStep", floor)

    def test_straight_path_check_counts_the_pre_start_steps(self):
        self.assertIn("f.directPathCrossesPack = PathCrossesUnclearedPack(*slot.run.plan,\n"
                      "                            TortoiseBots::DungeonRun::LiveFrom(slot.run, true), whole, f.team,\n"
                      "                            TortoiseBots::DungeonRun::UnclearedForPath(slot.run));", COMMANDS)

    def test_partial_approach_path_is_reported(self):
        approach = body(COMMANDS, "bool ApproachOnPath(Player* lead,", "\n}\n")
        # Round 1/3: a bare PATHFIND_INCOMPLETE (a point just off the mesh, a capped path still getting on) is walkable.
        self.assertIn("*incomplete = !PathWalkable((type & PATHFIND_NOPATH) != 0, (type & PATHFIND_INCOMPLETE) != 0,\n"
                      "            lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ(), end.x, end.y, end.z, x, y, z);", approach)
        # Before the no-path return, so a no path leaves it read too.
        self.assertLess(approach.index("*incomplete ="), approach.index("if (path.getPathType() & PATHFIND_NOPATH)"))
        self.assertIn("direct ? &f.approachIncomplete : nullptr);", COMMANDS)
        walk_back = body(POLICY, "inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)",
                         "\n}\n")
        self.assertIn("if (r.walkBackDirect && f.approachKnown && f.approachIncomplete)", walk_back)

    def test_stuck_walk_back_is_rescued_before_the_pause(self):
        walk = body(POLICY, "inline bool WalkTo(Run& r, const Facts& f, float x, float y, float z, bool direct,", "\n}\n")
        self.assertLess(walk.index("if (WalkBackStuckRescue(r, f, dist, x, y, z))"),
                        walk.index('Pause(r, "the lead bot is stuck "'))

    def test_retreat_key_is_read_with_the_config(self):
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        keys = body(config, "void PlayerbotAIConfig::LoadDungeonRunKeys()\n{", "\n}\n")
        self.assertIn('dungeonRunRetreatAt2Dead = config.GetBoolDefault("AiPlayerbot.DungeonRunRetreatAt2Dead", false);',
                      keys)
        self.assertIn("bool dungeonRunRetreatAt2Dead;", header)
        self.assertIn("\nAiPlayerbot.DungeonRunRetreatAt2Dead = 0\n", template)
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.retreatAt2Dead = sPlayerbotAIConfig.dungeonRunRetreatAt2Dead;", facts)
        self.assertIn("bool retreatAt2Dead = false;", POLICY)
        for doc in ("docs/server/settings/dungeons-raids.md", "docs/play/dungeon-run-leader.md"):
            self.assertIn("AiPlayerbot.DungeonRunRetreatAt2Dead", (ROOT / doc).read_text())

    def test_round_one_keys_are_read_with_the_config(self):
        config = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
        header = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
        template = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
        keys = body(config, "void PlayerbotAIConfig::LoadDungeonRunKeys()\n{", "\n}\n")
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        for key, member, fact in (("WalkBackRescue", "dungeonRunWalkBackRescue", "walkBackRescue"),
                                  ("FightForward", "dungeonRunFightForward", "fightForward"),
                                  ("LightRest", "dungeonRunLightRest", "lightRest")):
            self.assertIn('%s = config.GetBoolDefault("AiPlayerbot.DungeonRun%s", true);' % (member, key), keys)
            self.assertIn("bool %s;" % member, header)
            self.assertIn("\nAiPlayerbot.DungeonRun%s = 1\n" % key, template)
            self.assertIn("f.%s = sPlayerbotAIConfig.%s;" % (fact, member), facts)
            self.assertIn("bool %s = true;" % fact, POLICY)
            for doc in ("docs/server/settings/dungeons-raids.md", "docs/play/dungeon-run-leader.md"):
                self.assertIn("AiPlayerbot.DungeonRun%s" % key, (ROOT / doc).read_text())
        self.assertIn("ReadTimeSinkWays(slot, lead, f);", facts)

    def test_fight_forward_and_shortcut_reads(self):
        reads = body(COMMANDS, "void ReadTimeSinkWays(", "\n}\n")
        self.assertIn("f.forwardPathOk = DirectNavPath(lead, pull.x, pull.y, pull.z, way);", reads)
        self.assertIn("f.forwardLos = lead->IsWithinLOS(pull.x, pull.y, pull.z + 1.0f, true, 0.0f);", reads)
        self.assertIn("GetAttackDistance(lead) + kForwardAggroMarginYd;", reads)
        self.assertIn("<= kHopPullYd;", reads)
        self.assertIn("f.shortcutOk = DirectNavPath(lead, p.x, p.y, p.z, way);", reads)
        direct = body(COMMANDS, "bool DirectNavPath(", "\n}\n")
        # Round 2 (Codex, a4a16e14): a genuine navmesh route only, no SHORTCUT or NOT_USING_PATH fallback.
        self.assertIn("if (!NavPathReaches((type & PATHFIND_NOPATH) != 0, (type & PATHFIND_INCOMPLETE) != 0, (type & PATHFIND_SHORTCUT) != 0,\n"
                      "            (type & PATHFIND_NOT_USING_PATH) != 0, end.x, end.y, end.z, x, y, z))", direct)
        # Both users go through it; LegOnPath keeps its own (stricter: no incomplete path at all) test of the same flags.
        self.assertEqual(reads.count("DirectNavPath(lead,"), 2)
        leg = body(COMMANDS, "bool LegOnPath(", "\n}\n")
        self.assertIn("PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH | PATHFIND_SHORTCUT", leg)
        self.assertIn("PathFitsStraight(length, DistanceTo(lead, x, y, z))", direct)

    def test_retreat_runs_before_the_guard(self):
        tick = body(POLICY, "inline std::string TickBody(Run& r, const Facts& f, std::uint32_t dt, const RestLines& lines)\n{",
                    "\n}\n")
        self.assertLess(tick.index("if (RetreatTick(r, f, *s, dt, order))"), tick.index("if (CheckGuard(r, f, dt, order))"))
        self.assertIn("EndRetreat(r);", body(POLICY, "inline std::string Tick(Run& r, const Facts& raw,", "\n}\n"))

    def test_self_regroups_rearm_the_rescue(self):
        revived = body(POLICY, "inline bool LeadRevivedAway(Run& r, const Facts& f)", "\n}\n")
        # Three calls: the other part's (test_revived_in_another_part_is_rescued), the pause past kAutoRegroupsPerStep
        # (test_death_pauses_rearm_the_rescue), then the regroup's.
        self.assertEqual(revived.count("RearmRescueAfterDeath(r);"), 3)
        rearm = revived.index("ResetStep(r);\n    RearmRescueAfterDeath(r);\n    BeginRegroup(r);")
        self.assertGreater(rearm, revived.index('Pause(r, "the lead bot died and came back "'))
        wipe = body(POLICY, "if (autoRegroup && !f.anyInCombat)", "return order;")
        self.assertLess(wipe.index("ResetStep(r);"), wipe.index("RearmRescueAfterDeath(r);"))
        self.assertLess(wipe.index("RearmRescueAfterDeath(r);"), wipe.index("BeginRegroup(r);"))
        reset = body(POLICY, "inline void ResetStep(Run& r)", "\n}\n")
        self.assertNotIn("RearmRescueAfterDeath", reset)
        resume = body(POLICY, "inline bool Resume(Run& r)", "\n}\n")
        self.assertNotIn("RearmRescueAfterDeath", resume)

    def test_placed_rescue_targets_the_safe_point_by_the_wipe(self):
        placed = body(POLICY, "inline bool PlacedRunRescue(Run& r, const Facts& f)", "\n}\n")
        # Bot-only: the safe point on the way it came, first; the first safe point from the start only as the fall-back.
        players = placed.index("if (f.players.empty())")
        latest = placed.index("SafePointAtOrBefore(r, f.team, last)")
        first = placed.index("for (std::size_t j = r.startStep; !byTheWipe &&")
        self.assertLess(placed.index("LastReachedBefore(r, r.step)"), latest)
        self.assertLess(players, latest)
        self.assertLess(latest, first)
        self.assertIn("last >= r.startStep", placed)
        self.assertIn("RescueDue(r, f, target);", placed)
        self.assertNotIn("strandBotOnly", placed)

    def test_death_pauses_rearm_the_rescue(self):
        for start, pause in (
                ("inline bool LeadRevivedAway(Run& r, const Facts& f)", 'Pause(r, "the lead bot died and came back "'),
                ("inline bool WaitForFallen(Run& r, const Facts& f)", 'Pause(r, "the lead bot is dead: revive it, then .bot run resume ("'),
                ("    if (wipe && !autoRegroup && !r.wipeStopDue)\n", 'Pause(r, std::to_string(partyDead) + " dead: regroup (cmd revive party), then .bot run resume ("')):
            block = body(POLICY, start, pause)
            self.assertTrue(block.rstrip().endswith("RearmRescueAfterDeath(r); // a death: the resume's walk back may teleport once more"), start)
        # Resume and ResetStep still never re-arm (test_self_regroups_rearm_the_rescue), nor Pause itself.
        self.assertNotIn("RearmRescueAfterDeath", body(POLICY, "inline void Pause(Run& r, const std::string& why)", "\n}\n"))

    def test_walk_back_rescues_off_an_island(self):
        walk = body(POLICY, "inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)", "\n}\n")
        # The island rescue: after the replay's one restart, before the direct-step pause and the pass.
        island = walk.index("if (noWay && IslandWalkBackRescue(r, f, why))")
        self.assertLess(walk.index("r.walkBackRestarted = true;"), island)
        self.assertLess(island, walk.index("if (DirectStillToCross(r, r.walkBackIdx))"))
        self.assertLess(island, walk.index('PassWalkBackPoint(r, f.leadX, f.leadY, f.leadZ, "walk back: " + why);'))
        isle = body(POLICY, "inline bool IslandWalkBackRescue(Run& r, const Facts& f, const std::string& why)", "\n}\n")
        for part in ("r.noSpotKnown", "!f.players.empty()", "RescueAllowed(r, f)", "SafePointAtOrBefore(r, f.team, last)",
                     "RescueDue(r, f, target);"):
            self.assertIn(part, isle)
        self.assertIn("ReplayStartPoint(r, f)", body(POLICY, "inline bool ReplayWalkBack(Run& r, const Facts& f)\n{", "\n}\n"))

    def test_walk_back_rescues_before_a_long_replay(self):
        walk = body(POLICY, "inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)", "\n}\n")
        # Every straight-walk failure asks the rescue right before its replay.
        for ask in ('ReplayRescue(r, f, "no complete path to " + what + " from here")',
                    'ReplayRescue(r, f, "the straight way to " + what + " passes an uncleared pack")',
                    "ReplayRescue(r, f, why)"):
            at = walk.index(ask)
            self.assertLess(at, walk.index("if (ReplayWalkBack(r, f))", at))
        self.assertEqual(walk.count("ReplayRescue("), 3)
        self.assertEqual(walk.count("if (ReplayWalkBack(r, f))"), 3)
        rescue = body(POLICY, "inline bool ReplayRescue(Run& r, const Facts& f, const std::string& why)", "\n}\n")
        for part in ("!f.players.empty()", "RescueAllowed(r, f)", "SafePointAtOrBefore(r, f.team, last)",
                     "ReplayYardsTo(r, f, target)", "kReplayRescueYd", "RescueDue(r, f, target);"):
            self.assertIn(part, rescue)
        self.assertIn("ReplayStartPoint(r, f)", body(POLICY, "inline float ReplayYardsTo(", "\n}\n"))

    def test_backoff_asks_the_rescue_first(self):
        walk = body(POLICY, "inline bool WalkBack(Run& r, const Facts& f, std::uint32_t dt, std::string& order)", "\n}\n")
        # The backoff asks the rescue with its new target before it backs off (Shadowfang Keep r36 slot A).
        ask = walk.index('BackoffRescue(r, f, "the straight way to " + what + " passes an uncleared pack", back)')
        self.assertLess(walk.index("std::size_t const back = ClearPointBefore(r, f.team, r.walkBackIdx, kStraightBackoffYd);"),
                        ask)
        self.assertLess(ask, walk.index("++r.straightBackoffs;"))
        # The rescue answers; the backoff runs only when it said no (X3: the ask without `return true` survived).
        self.assertIn("BackoffRescue(r, f, \"the straight way to \" + what + \" passes an uncleared pack\", back))\n"
                      "                    return true;", walk)
        self.assertEqual(walk.count("BackoffRescue("), 1)
        rescue = body(POLICY, "inline bool BackoffRescue(Run& r, const Facts& f, const std::string& why, std::size_t back)",
                      "\n}\n")
        for part in ("!f.players.empty()", "RescueAllowed(r, f)", "SafePointAtOrBefore(r, f.team, last)",
                     "PointsYards(r, back, target)", "kReplayRescueYd", "RescueDue(r, f, target);",
                     "r.strandBotOnly = true;", "r.strandReplay = true;"):
            self.assertIn(part, rescue)

    def test_ceiling_pause_tracks_the_route(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        self.assertIn("TortoiseBots::DungeonRun::TrackRouteWhilePaused(slot.run, lead->IsAlive(),", tick)
        self.assertLess(tick.index("TortoiseBots::DungeonRun::TrackRouteWhilePaused(slot.run, lead->IsAlive(),"),
                        tick.index("TortoiseBots::DungeonRun::CeilingFightOverWhilePaused(slot.run"))

    def test_hole_branch_asks_the_rescue_first(self):
        start = body(POLICY, "inline void StartWalkBack(Run& r, const Facts& f)", "\n}\n")
        self.assertLess(start.index("if (HoleReplayRescue(r, f, hole))"),
                        start.index("r.walkBackIdx = LastPointBeforeDirect(r, hole);"))

    def test_passed_boss_pauses_before_the_wait(self):
        fight = body(POLICY, "inline void FightStep(Run& r, const Facts& f, const PlanStep& s,", "\n}\n")
        passed = fight.index("if (u.entry == s.entry && u.hostile && r.passed.count(u.guid))")
        self.assertLess(fight.index("else if (end == FightEnd::BossDown)"), passed)
        self.assertLess(passed, fight.index("r.bossWaitMs += dt;"))
        self.assertIn("FromLeadBot(ai::PlanDistance(f.leadX, f.leadY, f.leadZ, f.bossAliveX, f.bossAliveY, f.bossAliveZ),",
                      fight)

    def test_passed_boss_height_is_the_unit_real_one(self):
        # Codex review round 1: the passed-boss pause mixed the boss's real spot (u.distLead) with the plan point's
        # height (s.z - f.leadZ). The note reads u.z - f.leadZ, and the glue fills it from the creature's position.
        fight = body(POLICY, "inline void FightStep(Run& r, const Facts& f, const PlanStep& s,", "\n}\n")
        self.assertIn("FromLeadBot(u.distLead, u.z - f.leadZ)", fight)
        self.assertIn("unit.z = creature->GetPositionZ(); // the passed-boss pause reads the unit's real height off it",
                      COMMANDS)


if __name__ == "__main__":
    unittest.main()
