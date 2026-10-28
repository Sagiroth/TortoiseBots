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
    CHECK(SourceLevelFits(1, 1));

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
    CHECK(OrderVerdict(true, 2, false, true) == OrderReason::LowLevel);
    CHECK(OrderVerdict(false, 2, false, true) == OrderReason::NoSkill);
    CHECK(OrderVerdict(false, 1, false, true) == OrderReason::LowSkill);
    CHECK(OrderVerdict(false, 0, false, true) == OrderReason::NoRange);
    CHECK(OrderVerdict(false, 0, true, true) == OrderReason::NoSource);
    CHECK(OrderVerdict(false, 2, false, true, true) == OrderReason::NoRange);
    CHECK(OrderVerdict(false, 1, true, true, true) == OrderReason::LowSkill);

    CHECK(OrderModeSkipsGates(true, true));
    CHECK(!OrderModeSkipsGates(true, false));
    CHECK(!OrderModeSkipsGates(false, true));
    CHECK(!OrderModeSkipsGates(false, false));

    if (failures == 0)
        std::printf("test_guild_order_sources_policy ok\n");
    return failures ? 1 : 0;
}
