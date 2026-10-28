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
    CHECK(SkinningSkillForMobLevel(10) == 0);
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

    if (failures == 0)
        std::printf("test_guild_order_sources_policy ok\n");
    return failures ? 1 : 0;
}
