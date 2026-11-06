"""The lead bot's floor read counts a game-object surface (an elevator, a platform) as a floor.

DungeonRun::LeadOffFloor (runtime/DungeonRunPolicy.h, unit-tested in tools/test_dungeon_run_policy.cpp) knows only the
terrain floor and the mesh probe, so a lead bot on a game-object deck read as off the floor and blocked every summon.
The glue in commands/Commander.cpp now also passes a transport under the lead bot, or the core's own height read within
kSurfaceYd of the feet. Every summon site shares the 30 s pause, through Run::summonRefusedOffFloor and the tick's end.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/DungeonRunPolicy.h").read_text()


class LeadFloorSurfaceWiringTest(unittest.TestCase):
    def test_the_glue_reads_the_surface_and_the_transport(self):
        i = COMMANDER.index("f.leadOffFloor = mmapLoaded")
        glue = COMMANDER[i - 1500:i + 400]
        self.assertIn("surfaceMap->GetHeight(lead->GetPositionX(), lead->GetPositionY(), leadZ + 0.5f)", glue)
        self.assertIn("TortoiseBots::DungeonRun::LeadOnSurface(leadZ, lead->GetTransport() != nullptr,", glue)
        self.assertIn("lead->IsInWater() || lead->IsUnderwater(), onSurface);", glue)

    def test_every_summon_site_shares_the_pause(self):
        self.assertIn("r.summonRefusedOffFloor = true;", POLICY)
        self.assertEqual(POLICY.count("r.summonRefusedOffFloor = false;"), 2)  # LeadOffFloorPauseTick and Tick's start
        tick = POLICY[POLICY.index("inline std::string Tick(Run& r, const Facts& raw"):]
        self.assertLess(tick.index("r.summonRefusedOffFloor = false;"), tick.index("TickBody(r, f, dt, lines)"))
        self.assertLess(tick.index("TickBody(r, f, dt, lines)"), tick.index("LeadOffFloorPauseTick(r, f);"))


if __name__ == "__main__":
    unittest.main()
