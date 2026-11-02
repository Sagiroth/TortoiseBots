"""An order's farm zone is the nearest servable one; a zone is skipped only after two failed targets.

The source planner ranks its zones by the distance to the sample farm point on
the bot's map (same map first) among the zones the travel layer's route gate
accepts, and drops a zone from the plan once two different target points in it
failed to travel inside the policy window. The travel layer records each failed
point per bot and zone in the order store; the planner asks the store back.
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = (ROOT / "ai/playerbot/GuildOrderSources.cpp").read_text()
STORE = (ROOT / "runtime/GuildOrderStore.cpp").read_text()
TRAVEL = (ROOT / "ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp").read_text()
POLICY = (ROOT / "runtime/GuildOrderSourcesPolicy.h").read_text()
CHOOSE = (ROOT / "ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp").read_text()
HEADER = (ROOT / "ai/playerbot/GuildOrderSources.h").read_text()
TRAVEL_MGR = (ROOT / "ai/playerbot/TravelMgr.cpp").read_text()
TRAVEL_MGR_HEADER = (ROOT / "ai/playerbot/TravelMgr.h").read_text()


def planner_body():
    start = SOURCES.index("SourcePlan PlanGuildOrderSources(")
    end = SOURCES.index("std::string PlanOrderDifficulty(", start)
    return SOURCES[start:end]


class GuildOrderNearestFarmWiringTest(unittest.TestCase):
    def test_planner_ranks_nearest_first(self):
        body = planner_body()
        self.assertIn("std::sort(plan.zones.begin(), plan.zones.end(), FarmZoneBefore)", body)
        self.assertIn("zone.distanceSq = accum.hasNearest ? accum.nearest : -1.0f", body)

    def test_planner_asks_the_travel_route_gate(self):
        body = planner_body()
        self.assertIn("SharedRouteIsSurvivable(bot, &sample, blocker)", body)
        self.assertIn(
            "WorldPosition sample(botPos.getMapId(), accum.nearestX, accum.nearestY, accum.nearestZ)",
            body)
        self.assertIn("if (rankByTravelRoute && accum.hasNearest)", body)
        self.assertEqual(body.count("SharedRouteIsSurvivable("), 1)

    def test_only_the_travel_pick_ranks_by_route(self):
        self.assertIn("bool rankByTravelRoute = false);", HEADER)
        self.assertIn(
            "return PlanGuildOrderSources(bot, planItem, TortoiseBots::GuildOrders::OrderType::Farm,\n                    true);",
            CHOOSE)

    def test_planner_drops_a_zone_after_two_failed_targets(self):
        body = planner_body()
        self.assertIn("FarmZoneSkipped(FarmZoneFailures(bot->GetGUIDLow(), zone.zoneId)", body)
        self.assertIn("DropUnreachableFarmZones(plan.zones, unreachable)", body)
        self.assertNotIn("FarmZoneUnreachableAt(", body)

    def test_policy_wants_two_distinct_targets(self):
        self.assertIn("inline constexpr size_t kFarmZoneFailedTargets = 2;", POLICY)
        self.assertIn("return FarmZoneFailedTargets(failures, nowMs) >= kFarmZoneFailedTargets;", POLICY)

    def test_travel_notes_only_mesh_failed_order_trips(self):
        idx = TRAVEL.index("NoteFarmZoneTargetFailed(")
        window = TRAVEL[max(0, idx - 600):idx]
        self.assertIn('purpose == "guild order"', window)
        self.assertIn("TravelPathUnreachable(pathTag)", window)
        self.assertIn("location.GetArea()", window)
        self.assertEqual(TRAVEL.count("NoteFarmZoneTargetFailed("), 1)

    def test_store_remembers_each_failed_point(self):
        start = STORE.index("void NoteFarmZoneTargetFailed(uint32_t botGuidLow, uint32_t zoneId")
        end = STORE.index("void NoteSearchFailed(uint32_t botGuidLow", start)
        body = STORE[start:end]
        self.assertIn("RecordFarmZoneTargetFailure(it->second.farmZoneFailures[zoneId]", body)
        self.assertIn("FarmZoneTargetPointAt(mapId, x, y, z)", body)
        self.assertIn("record.farmZoneId != zoneId", body)
        self.assertIn("record.supplyStep != SupplyStep::Farm", body)
        self.assertNotIn("farmZoneUnreachableMs", STORE)

    def test_fish_zone_takes_its_spot_not_the_origin(self):
        body = planner_body()
        fish = body[body.index("if (source.flag != SourceFish)"):body.index("for (GuildOrderEntrySource const& source : found->second)", body.index("if (source.flag != SourceFish)"))]
        self.assertNotIn("accum.nearest = 0.0f", fish)
        self.assertNotIn("accum.hasNearest = true;\n            accum.nearest = 0", fish)
        self.assertNotIn("if (area->MapId == int32_t(botFishingMap))", fish)
        self.assertIn("sTravelMgr.FishZoneNearestSpot(zoneId, botPos, spot, spotDistSq)", fish)
        self.assertIn("accum.nearestX = spot.getX();", fish)
        self.assertIn("accum.nearestZ = spot.getZ();", fish)
        self.assertIn("(!accum.hasNearest || spotDistSq < accum.nearest)", fish)

    def test_fish_spot_lookup_is_same_map_squared_distance(self):
        start = TRAVEL_MGR.index("bool TravelMgr::FishZoneNearestSpot(")
        end = TRAVEL_MGR.index("\n}\n", start)
        body = TRAVEL_MGR[start:end]
        self.assertIn("candidate.getMapId() != from.getMapId()", body)
        self.assertIn("dist = dx * dx + dy * dy + dz * dz", body)
        self.assertIn("dist < distanceSq", body)
        self.assertIn("fishSpotsByZone.find(zoneId)", body)
        self.assertIn("bool FishZoneNearestSpot(uint32 zoneId, WorldPosition const& from, WorldPosition& spot, float& distanceSq) const;",
                      TRAVEL_MGR_HEADER)

    def test_fish_spots_are_kept_per_zone_at_load(self):
        self.assertIn("fishSpotsByZone[zone].emplace_back(mapId, positionX, positionY, positionZ, orientation);",
                      TRAVEL_MGR)
        self.assertIn("std::unordered_map<uint32, std::vector<WorldPosition>> fishSpotsByZone;", TRAVEL_MGR_HEADER)
        self.assertNotIn("fishSpotZones", TRAVEL_MGR)
        self.assertNotIn("fishSpotZones", TRAVEL_MGR_HEADER)


if __name__ == "__main__":
    unittest.main()
