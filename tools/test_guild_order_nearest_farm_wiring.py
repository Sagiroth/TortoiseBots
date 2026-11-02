"""An order's farm zone is the nearest servable one, and a mesh-failed zone is skipped.

The source planner ranks its zones by the distance to the sample farm point
on the bot's map (same map first) and drops zones whose last farm-leg travel
probe found no route, for the policy window. The travel layer records that
probe per bot and zone in the order store; the planner asks the store back.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = (ROOT / "ai/playerbot/GuildOrderSources.cpp").read_text()
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
TRAVEL = (ROOT / "ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp").read_text()


def planner_body():
    start = SOURCES.index("SourcePlan PlanGuildOrderSources(")
    end = SOURCES.index("std::string PlanOrderDifficulty(", start)
    return SOURCES[start:end]


class GuildOrderNearestFarmWiringTest(unittest.TestCase):
    def test_planner_ranks_nearest_first(self):
        body = planner_body()
        self.assertIn("std::sort(plan.zones.begin(), plan.zones.end(), FarmZoneBefore)", body)
        self.assertIn("zone.distanceSq = accum.hasNearest ? accum.nearest : -1.0f", body)

    def test_planner_drops_mesh_failed_zones(self):
        body = planner_body()
        self.assertIn("FarmZoneUnreachableAt(bot->GetGUIDLow(), zone.zoneId)", body)
        self.assertIn("FarmZoneFailureFresh(", body)
        self.assertIn("DropUnreachableFarmZones(plan.zones, unreachable)", body)

    def test_travel_notes_only_mesh_failed_order_trips(self):
        idx = TRAVEL.index("NoteFarmZoneUnreachable(")
        window = TRAVEL[max(0, idx - 500):idx]
        self.assertIn('purpose == "guild order"', window)
        self.assertIn("TravelPathUnreachable(pathTag)", window)
        self.assertIn("location.GetArea()", window)
        self.assertEqual(TRAVEL.count("NoteFarmZoneUnreachable("), 1)

    def test_store_remembers_per_bot_and_zone(self):
        start = STORE.index("void NoteFarmZoneUnreachable(uint32_t botGuidLow, uint32_t zoneId)")
        end = STORE.index("void NoteSearchFailed(uint32_t botGuidLow", start)
        body = STORE[start:end]
        self.assertIn("farmZoneUnreachableMs[zoneId] = WorldTimer::getMSTime()", body)
        self.assertIn("record.farmZoneId != zoneId", body)
        self.assertIn("record.supplyStep != SupplyStep::Farm", body)


if __name__ == "__main__":
    unittest.main()
