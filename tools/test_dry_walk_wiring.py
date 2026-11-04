"""The run leader's dry walk (AiPlayerbot.DungeonRunDryWalk) glue.

ZG run 35 f3 and run 36 c3-r2/b2: the core's player filter includes NAV_WATER, so
the lead bot's path crossed the pool on its surface; an approach hop or a too-far
leg picked there landed in the water, and the core's straight swim chord (0x81)
held the lead against a bank. The policy is tested in
tools/test_dungeon_run_policy.cpp; this pins the glue in commands/Commander.cpp:
- DryWayPath asks Detour for a NAV_GROUND-only path (never the shim's
  NAV_MAGMA_SLIME / NAV_GROUND_STEEP, which name other bits in this core);
- the walk's own order is intercepted before IssueRunOrder (DryWalkWanted,
  DryLegPick, DryLegTaken), and a refused dry leg falls back to the walk's order
  once in the same tick;
- ApproachOnPath, ReadPullStand and DirectNavPath read the dry flag;
- the key loads with default false and the shipped template sets 0.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDS = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()
CONFIG = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONF = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()


def body(text, start, end):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class DryWayPathWiringTest(unittest.TestCase):
    def test_ground_only_filter(self):
        fn = body(COMMANDS, "static bool DryWayPath(Player* lead,", "\n}\n")
        self.assertIn("filter.setIncludeFlags(NAV_GROUND);", fn)
        self.assertIn("filter.setExcludeFlags(0);", fn)
        # This core's NavTerrain: 0x18 is WATER|STEEP_SLOPES and 0x02 is MAGMA, not the shim's names.
        self.assertNotIn("NAV_MAGMA_SLIME", fn)
        self.assertNotIn("NAV_GROUND_STEEP", fn)

    def test_detour_query_and_partial_result(self):
        fn = body(COMMANDS, "static bool DryWayPath(Player* lead,", "\n}\n")
        self.assertIn("MMAP::MMapFactory::createOrGetMMapManager()", fn)
        self.assertIn("query->findNearestPoly(start, startExtents, &filter, &startRef, startNearest)", fn)
        self.assertIn("query->findNearestPoly(end, endExtents, &filter, &endRef, endNearest)", fn)
        self.assertIn("query->findPath(startRef, endRef, startNearest, endNearest, &filter, polys, &polyCount, 256)", fn)
        self.assertIn("query->findStraightPath(startNearest, endNearest, polys, polyCount, straight, nullptr, nullptr,", fn)
        # A complete Detour route may still end on a snapped poly another floor away: the endpoint bound holds too.
        self.assertIn("PathEndsShort(false, true, endX, endY, endZ, x, y, z)", fn)


class WalkInterceptWiringTest(unittest.TestCase):
    def test_walk_order_is_intercepted_before_the_issue(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        i = tick.index("TortoiseBots::DungeonRun::DryWalkWanted(it->second.run, facts, order)")
        self.assertLess(tick.index("order = TortoiseBots::DungeonRun::Tick(slot.run, facts, dt);"), i)
        issue = tick.index("bool accepted = IssueRunOrder(commander, order, answer,", i)
        self.assertLess(i, issue)
        block = tick[i:issue]
        self.assertIn("DryWayFor(lead, run.walkX, run.walkY, run.walkZ, dry)", block)
        self.assertIn("DryLegPick(dry,", block)
        self.assertIn("CorePathSwims(lead, p.x, p.y, p.z)", block)
        self.assertIn("TooFarLegRepeats(run, leg.x, leg.y, leg.z)", block)
        self.assertIn("order = TortoiseBots::DungeonRun::MoveOrder(leg.x, leg.y, leg.z, false);", block)

    def test_accepted_leg_and_refused_fallback(self):
        tick = body(COMMANDS, "void TickDungeonRun(uint64 commanderRaw, uint32 dt)", "\n}\n")
        i = tick.index("if (dryLeg)")
        block = tick[i:tick.index("// A raid boss fight's healer guard", i)]
        self.assertIn("TortoiseBots::DungeonRun::DryLegTaken(it->second.run, facts, dryLegX, dryLegY, dryLegZ);", block)
        self.assertIn("order = walkOrder;", block)
        self.assertIn('answer += " (the dry leg was refused: " + legAnswer + ")";', block)
        # The fallback's own order is issued after the run is looked up again.
        self.assertLess(block.index("order = walkOrder;"), block.index("accepted = IssueRunOrder(commander, order, answer,"))


class DryFlagReadersTest(unittest.TestCase):
    def test_approach_hop_reads_the_dry_way(self):
        fn = body(COMMANDS, "bool ApproachOnPath(Player* lead,", "\n}\n")
        self.assertIn("bool dryWalk = false", fn)
        self.assertIn("if (DryWayFor(lead, x, y, z, dry))", fn)
        self.assertIn("if (whole)\n                whole->insert(whole->end(), dry.begin(), dry.end());", fn)
        self.assertIn("if (dryWalk)", fn)

    def test_firing_stand_refuses_a_wet_point(self):
        fn = body(COMMANDS, "void ReadPullStand(", "\n}\n")
        self.assertIn("if (f.dryWalk && DryWayFor(lead, unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), dry))", fn)
        self.assertIn("if (dryWalk && map->GetTerrain()->IsSwimmable(candidate.x, candidate.y, candidate.z))", fn)
        self.assertIn("clearance || f.dryWalk ? kStandClearCandidates : 1", fn)

    def test_direct_nav_path_takes_the_chord_as_a_fallback(self):
        fn = body(COMMANDS, "bool DirectNavPath(Player* lead,", "\n}\n")
        self.assertIn("bool dryWalk = false", fn)
        self.assertIn("bool const fallback = (type & PATHFIND_NOT_USING_PATH) != 0 || (dryWalk && (type & PATHFIND_UNDERWATER) != 0);", fn)
        self.assertIn("fallback, end.x, end.y, end.z, x, y, z))", fn)

    def test_facts_read_the_key(self):
        facts = body(COMMANDS, "void ReadRunFacts(", "\n}\n")
        self.assertIn("f.dryWalk = sPlayerbotAIConfig.dungeonRunDryWalk;", facts)
        for call in ("DirectNavPath(lead, pull.x, pull.y, pull.z, way, f.dryWalk)",
                     "DirectNavPath(lead, p.x, p.y, p.z, way, f.dryWalk)",
                     "DirectNavPath(tank, targetX, targetY, targetZ, way, f.dryWalk)"):
            self.assertIn(call, COMMANDS)
        # Every approach hop call hands the tick's flag on (the walk back's one spans two lines) and keeps the
        # incomplete out-parameter slot: a short call passes the flag as a bool* and does not compile.
        calls = COMMANDS.split("f.approachKnown = ApproachOnPath(lead, step->x")[1:]
        self.assertEqual(len(calls), 5)
        for rest in calls:
            self.assertIn("f.dryWalk);", rest[:rest.index(";") + 1])
        self.assertEqual(COMMANDS.count("nullptr, &f, nullptr, f.dryWalk);"), 4)


class DryWalkKeyWiringTest(unittest.TestCase):
    def test_default_is_false(self):
        self.assertIn('dungeonRunDryWalk = config.GetBoolDefault("AiPlayerbot.DungeonRunDryWalk", false);', CONFIG)

    def test_template_ships_zero(self):
        self.assertIn("AiPlayerbot.DungeonRunDryWalk = 0", CONF)

    def test_policy_has_its_own_tests(self):
        tests = (ROOT / "tools/test_dungeon_run_policy.cpp").read_text()
        self.assertIn("static void TestZgC3HopDipsIntoThePool()", tests)
        self.assertIn("TestDryLegPickShrinks();", tests)
        self.assertIn("inline bool DryWalkWanted(", POLICY)


if __name__ == "__main__":
    unittest.main()
