// Standalone test for runtime/GuildOrderSourcesPolicy.h: the pure guild-order
// source rules (level window, skinning skill, chance filter, zone scoring).
#include "../runtime/GuildOrderSourcesPolicy.h"

#include <cstdio>

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

    CHECK(FarPartitionSkipApplies(false));
    CHECK(!FarPartitionSkipApplies(true));

    if (failures == 0)
        std::printf("test_guild_order_sources_policy ok\n");
    return failures ? 1 : 0;
}
