// Standalone test for runtime/GuildOrderSourcesPolicy.h: the pure guild-order
// source rules (level window, skinning skill, chance filter, zone scoring).
#include "../runtime/GuildOrderSourcesPolicy.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(kSourceLevelMargin == 3);
    CHECK(kGatherTripMinLevel == 10);
    CHECK(kMinFarmDropChancePct == 1.0f);

    CHECK(SourceLevelFits(27, 24));
    CHECK(SourceLevelFits(30, 30));
    CHECK(!SourceLevelFits(28, 24));
    CHECK(!SourceLevelFits(0, 24));
    // Faction hostility (the planner refuses creatures the bot cannot
    // attack): hostile by mask pair either way, by either side's enemy
    // list, and not hostile when neither side's masks or lists say so
    // (neutral same-faction mobs like the Rebel Watchman). Masks mirror
    // the core FactionMasks (player 1, alliance 2, horde 4, monster 8).
    {
        FactionTemplateMasks ally = { 1, 3, 12, { 0, 0, 0, 0 } };
        FactionTemplateMasks monster = { 14, 8, 3, { 0, 0, 0, 0 } };
        FactionTemplateMasks sameSide = { 11, 3, 0, { 0, 0, 0, 0 } };
        CHECK(FactionTemplatesHostile(ally, monster));
        CHECK(FactionTemplatesHostile(monster, ally));
        CHECK(!FactionTemplatesHostile(ally, sameSide));
        FactionTemplateMasks listed = { 90, 8, 0, { 0, 0, 0, 0 } };
        FactionTemplateMasks hater = { 2, 5, 0, { 90, 0, 0, 0 } };
        CHECK(FactionTemplatesHostile(hater, listed));
        CHECK(FactionTemplatesHostile(listed, hater));
        CHECK(!FactionTemplatesHostile(ally, ally));
    }

    CHECK(SkinningSkillForMobLevel(1) == 1);
    CHECK(SkinningSkillForMobLevel(9) == 1);
    CHECK(SkinningSkillForMobLevel(10) == 1);
    CHECK(SkinningSkillForMobLevel(15) == 50);
    CHECK(SkinningSkillForMobLevel(19) == 90);
    CHECK(SkinningSkillForMobLevel(20) == 100);
    CHECK(SkinningSkillForMobLevel(30) == 150);
    CHECK(SkinningSkillForMobLevel(60) == 300);

    CHECK(ZoneScore(10, 50.0f) == ZoneScore(25, 20.0f));
    CHECK(ZoneScore(10, 50.0f) > ZoneScore(10, 20.0f));
    CHECK(ZoneScore(0, 100.0f) == 0.0);

    CHECK(DropChanceFarmable(1.0f, false));
    CHECK(DropChanceFarmable(50.0f, false));
    CHECK(!DropChanceFarmable(0.9f, false));
    CHECK(!DropChanceFarmable(50.0f, true));
    CHECK(!DropChanceFarmable(0.0f, false));

    CHECK(EqualShareChance(0) == 0.0f);
    CHECK(EqualShareChance(4) == 25.0f);
    CHECK(MemberChance(30.0f, 25.0f) == 30.0f);
    CHECK(MemberChance(0.0f, 25.0f) == 25.0f);
    CHECK(ReferenceGate(50.0f, 25.0f) == 0.5f);
    CHECK(ReferenceGate(0.0f, 25.0f) == 0.25f);
    CHECK(ReferenceGate(100.0f, 25.0f) == 1.0f);
    CHECK(ReferenceGate(0.0f, 0.0f) == 0.0f);

    CHECK(OrderVerdict(false, 0, false, false) == OrderReason::NoSource);
    CHECK(OrderVerdict(false, 0, false, false, false, true) == OrderReason::Elite);
    CHECK(OrderVerdict(false, 0, true, true, false, true) == OrderReason::NoSource);
    CHECK(OrderVerdict(true, 2, false, true) == OrderReason::LowLevel);
    CHECK(OrderVerdict(false, 2, false, true) == OrderReason::NoSkill);
    CHECK(OrderVerdict(false, 1, false, true) == OrderReason::LowSkill);
    CHECK(OrderVerdict(false, 0, false, true) == OrderReason::NoRange);
    CHECK(OrderVerdict(false, 0, true, true) == OrderReason::NoSource);
    CHECK(OrderVerdict(false, 2, false, true, true) == OrderReason::NoRange);
    CHECK(OrderVerdict(false, 1, true, true, true) == OrderReason::LowSkill);
    CHECK(CraftRecipeVerdict(true) == OrderReason::None);
    CHECK(CraftRecipeVerdict(false) == OrderReason::NoRecipe);
    CHECK(OrderModeSkipsGates(true, true));
    CHECK(!OrderModeSkipsGates(true, false));
    CHECK(!OrderModeSkipsGates(false, true));
    CHECK(!OrderModeSkipsGates(false, false));

    CHECK(CreatureRankExcludedFromSoloFarm(1));
    CHECK(CreatureRankExcludedFromSoloFarm(2));
    CHECK(CreatureRankExcludedFromSoloFarm(3));
    CHECK(!CreatureRankExcludedFromSoloFarm(0));
    CHECK(!CreatureRankExcludedFromSoloFarm(4));

    CHECK(ExpectedKills(10, 40.0f) == 25.0);
    CHECK(ExpectedKills(20, 100.0f) == 20.0);
    CHECK(ExpectedKills(5, 0.22f) > 2000.0);
    CHECK(ExpectedKills(10, 0.0f) > 200.0);

    CHECK(EstimateDifficulty(20.0) == OrderDifficulty::Easy);
    CHECK(EstimateDifficulty(30.0) == OrderDifficulty::Easy);
    CHECK(EstimateDifficulty(31.0) == OrderDifficulty::Medium);
    CHECK(EstimateDifficulty(200.0) == OrderDifficulty::Medium);
    CHECK(EstimateDifficulty(201.0) == OrderDifficulty::Hard);
    CHECK(EstimateDifficulty(ExpectedKills(10, 40.0f)) == OrderDifficulty::Easy);
    CHECK(EstimateDifficulty(ExpectedKills(5, 0.22f)) == OrderDifficulty::Hard);
    CHECK(EstimateDifficulty(ExpectedKills(20, 100.0f)) == OrderDifficulty::Easy);
    CHECK(EstimateDifficulty(ExpectedKills(200, 100.0f)) == OrderDifficulty::Medium);

    CHECK(OutgrownFloorApplies(false));
    CHECK(!OutgrownFloorApplies(true));

    // Fallback tier: one level past the plan, still inside the grind cap.
    CHECK(kFallbackLevelMargin == 4);
    CHECK(FallbackSourceLevelFits(34, 30));
    CHECK(FallbackSourceLevelFits(27, 24));
    CHECK(!FallbackSourceLevelFits(35, 30));
    CHECK(!FallbackSourceLevelFits(0, 30));
    CHECK(!SourceLevelFits(34, 30) && FallbackSourceLevelFits(34, 30));
    // Solo farm legs never plan instance zones: the open-world continents
    // pass, dungeons/raids/BGs (Deadmines 36, Scarlet Monastery 43, the
    // custom 822 map) do not.
    CHECK(FarmZoneMapSoloWalkable(0));
    CHECK(FarmZoneMapSoloWalkable(1));
    CHECK(FarmZoneMapSoloWalkable(530));
    CHECK(FarmZoneMapSoloWalkable(571));
    CHECK(!FarmZoneMapSoloWalkable(36));
    CHECK(!FarmZoneMapSoloWalkable(43));
    CHECK(!FarmZoneMapSoloWalkable(822));

    CHECK(FarPartitionSkipApplies(false));
    CHECK(!FarPartitionSkipApplies(true));

    // A failed farm target is remembered per point inside the window, never
    // forever; the stamp is uptime ms and may wrap.
    CHECK(!FarmZoneFailureFresh(0, 12345));
    CHECK(FarmZoneFailureFresh(1000, 1000 + kFarmZoneUnreachableMs - 1));
    CHECK(!FarmZoneFailureFresh(1000, 1000 + kFarmZoneUnreachableMs));
    CHECK(FarmZoneFailureFresh(0xFFFFFF00u, 0x00001000u));
    CHECK(TravelPathUnreachable("incomplete"));
    CHECK(TravelPathUnreachable("nopath"));
    CHECK(!TravelPathUnreachable("complete"));
    CHECK(!TravelPathUnreachable("not-using-path"));
    CHECK(!TravelPathUnreachable("crossmap"));

    // One failed target point never costs the zone: the leg tries the zone's
    // other farm spots first. Two different points inside the window do.
    {
        uint32_t const now = 100000;
        std::vector<FarmZoneTargetFailure> zoneFailures;
        CHECK(!FarmZoneSkipped(zoneFailures, now));

        FarmZoneTargetPoint const a = FarmZoneTargetPointAt(0, 100.0f, 200.0f, 30.0f);
        FarmZoneTargetPoint const b = FarmZoneTargetPointAt(0, 400.0f, 500.0f, 60.0f);
        // Same 10 yd cell, different rounding noise: still one target.
        FarmZoneTargetPoint const aAgain = FarmZoneTargetPointAt(0, 104.0f, 205.0f, 31.0f);
        // Same coordinates on another map is another target.
        FarmZoneTargetPoint const aOtherMap = FarmZoneTargetPointAt(1, 100.0f, 200.0f, 30.0f);
        CHECK(a == aAgain);
        CHECK(!(a == b));
        CHECK(!(a == aOtherMap));

        RecordFarmZoneTargetFailure(zoneFailures, a, now);
        CHECK(zoneFailures.size() == 1);
        CHECK(!FarmZoneSkipped(zoneFailures, now));
        CHECK(FarmZoneFailedTargets(zoneFailures, now) == 1);

        RecordFarmZoneTargetFailure(zoneFailures, aAgain, now + 1000);
        CHECK(zoneFailures.size() == 1); // repeat at the same spot refreshes, adds no target
        CHECK(!FarmZoneSkipped(zoneFailures, now + 1000));

        RecordFarmZoneTargetFailure(zoneFailures, b, now + 2000);
        CHECK(FarmZoneFailedTargets(zoneFailures, now + 2000) == 2);
        CHECK(FarmZoneSkipped(zoneFailures, now + 2000));

        // A third point on another map does not change the count.
        RecordFarmZoneTargetFailure(zoneFailures, aOtherMap, now + 3000);
        CHECK(FarmZoneFailedTargets(zoneFailures, now + 3000) == 3);

        // Only the two fresh points count: the first two age out, so the zone
        // stops being skipped once its zoneFailures leave the window.
        CHECK(FarmZoneFailedTargets(zoneFailures, now + kFarmZoneUnreachableMs + 2500) == 1);
        CHECK(!FarmZoneSkipped(zoneFailures, now + kFarmZoneUnreachableMs + 2500));
        CHECK(!FarmZoneSkipped(zoneFailures, now + kFarmZoneUnreachableMs + 3500));

        // Stale stamps drop when the next failure is recorded.
        RecordFarmZoneTargetFailure(zoneFailures, b, now + kFarmZoneUnreachableMs + 4000);
        CHECK(zoneFailures.size() == 1);
        CHECK(!FarmZoneSkipped(zoneFailures, now + kFarmZoneUnreachableMs + 4000));

        // A zero clock never records (0 is the "never failed" stamp).
        std::vector<FarmZoneTargetFailure> zeroClock;
        RecordFarmZoneTargetFailure(zeroClock, a, 0);
        CHECK(zeroClock.empty());
    }

    // Nearest first, but a zone the travel layer's route gate rejects ranks
    // after the nearer-or-farther zones it accepts.
    {
        FarmZone rejected;
        rejected.zoneId = 1;
        rejected.onBotMap = true;
        rejected.distanceSq = 25.0f;
        rejected.score = 900.0;
        rejected.reachable = false;
        FarmZone accepted = rejected;
        accepted.zoneId = 2;
        accepted.distanceSq = 2500.0f;
        accepted.score = 5.0;
        accepted.reachable = true;

        std::vector<FarmZone> order { rejected, accepted };
        std::sort(order.begin(), order.end(), FarmZoneBefore);
        CHECK(order[0].zoneId == 2);
        CHECK(FarmZoneBefore(accepted, rejected));
        CHECK(!FarmZoneBefore(rejected, accepted));

        // A reachable cross-map zone still ranks after every same-map zone.
        FarmZone seaAccepted = accepted;
        seaAccepted.zoneId = 3;
        seaAccepted.onBotMap = false;
        CHECK(FarmZoneBefore(rejected, seaAccepted));
    }

    // Two servable zones: the near one is picked even though the far one is
    // richer; a zone on another continent ranks after both. Then the near
    // zone is dropped and the pick moves on.
    {
        FarmZone near;
        near.zoneId = 10;
        near.onBotMap = true;
        near.distanceSq = 25.0f;
        near.score = 10.0;
        FarmZone far;
        far.zoneId = 20;
        far.onBotMap = true;
        far.distanceSq = 900.0f;
        far.score = 500.0;
        FarmZone sea;
        sea.zoneId = 30;
        sea.onBotMap = false;
        sea.score = 9000.0;

        std::vector<FarmZone> zones { far, sea, near };
        std::sort(zones.begin(), zones.end(), FarmZoneBefore);
        CHECK(zones.size() == 3);
        CHECK(zones[0].zoneId == 10);
        CHECK(zones[1].zoneId == 20);
        CHECK(zones[2].zoneId == 30);
        CHECK(FarmZoneBefore(near, sea));
        CHECK(!FarmZoneBefore(sea, near));

        std::vector<uint8_t> nearFailed { 1, 0, 0 };
        DropUnreachableFarmZones(zones, nearFailed);
        CHECK(zones.size() == 2);
        CHECK(zones[0].zoneId == 20);
        CHECK(zones[1].zoneId == 30);

        std::vector<FarmZone> allFlagged { near, far };
        std::vector<uint8_t> all { 1, 1 };
        DropUnreachableFarmZones(allFlagged, all);
        CHECK(allFlagged.size() == 2); // no reachable zone: keep the plan, the order must not read nosource
    }

    if (failures == 0)
        std::printf("test_guild_order_sources_policy ok\n");
    return failures ? 1 : 0;
}
