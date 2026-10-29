// Standalone test for runtime/GuildOrderFishingPolicy.h: the pure guild
// fishing-order rules (gear bonuses, zone reach, cheapest pole pick).
#include "../runtime/GuildOrderFishingPolicy.h"
#include "../runtime/GuildOrderSourcesPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    CHECK(BestFishingPoleBonus() == 35);
    CHECK(kBestFishingLureBonus == 100);

    // Cities (-20) and quest zones (-70) always count, whatever the skill.
    CHECK(FishingZoneReachable(-20, 1));
    CHECK(FishingZoneReachable(-70, 1));
    CHECK(FishingZoneReachable(0, 1));

    // Skill alone reaches: no grant needed but reachable.
    CHECK(FishingZoneReachable(55, 300));
    CHECK(FishingPoleFor(55, 300) == 0);

    // Lure alone bridges small gaps: 55 needs 1 + 100, no pole granted.
    CHECK(FishingZoneReachable(55, 1));
    CHECK(FishingPoleFor(55, 1) == 0);

    // A pole bridges the middle: zone 130 with skill 1 needs +29 more
    // after the lure, so the +35 Arcanite pole is the cheapest that fits.
    CHECK(FishingZoneReachable(130, 1));
    CHECK(FishingPoleFor(130, 1) == 19970);

    // Cheapest pole wins: zone 130 with skill 20 needs +10 after the
    // lure, so Darkwood (+15) fits while Strong (+5) does not.
    CHECK(FishingPoleFor(130, 20) == 6366);

    // Big Iron (+20) tier: zone 130 with skill 10 needs +20 after lure.
    CHECK(FishingPoleFor(130, 10) == 6367);

    // Unreachable even with the best gear: zone 330 with skill 1.
    CHECK(!FishingZoneReachable(330, 1));
    CHECK(FishingPoleFor(330, 1) == 0);

    // High skill needs no grant for high zones either.
    CHECK(FishingZoneReachable(330, 300));
    CHECK(FishingPoleFor(330, 300) == 0);

    // The grant only matters when the skill alone falls short.
    CHECK(FishingGearHelps(1, 130));
    CHECK(!FishingGearHelps(300, 55));
    CHECK(!FishingGearHelps(1, -20));
    // Verdict witness for the fish loop: any fish row makes the item
    // sourced, and one on the bot's continent makes it ranged.
    {
        bool sawAny = false, sawInRange = false;
        FishSourceWitness(sawAny, sawInRange, true);
        CHECK(sawAny && sawInRange);
        CHECK(OrderVerdict(false, 1, sawInRange, sawAny) == OrderReason::LowSkill);
    }
    {
        bool sawAny = false, sawInRange = false;
        FishSourceWitness(sawAny, sawInRange, false);
        CHECK(sawAny && !sawInRange);
        CHECK(OrderVerdict(false, 0, sawInRange, sawAny) == OrderReason::NoRange);
    }
    {
        bool sawAny = false, sawInRange = false;
        FishSourceWitness(sawAny, sawInRange, true);
        CHECK(OrderVerdict(false, 2, sawInRange, sawAny) == OrderReason::NoSkill);
    }

    if (failures == 0)
        std::printf("test_guild_order_fishing_policy ok\n");
    return failures ? 1 : 0;
}
