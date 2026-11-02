// Standalone test for the fresh-seed quality floor (issue #1485, F2): a bot at
// levels 5-19 is seeded from white upwards (no grey), and from level 20 the band
// is unchanged (green and up).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_gear_seed_quality_policy.cpp -o /tmp/test_gear_seed_quality_policy
//   /tmp/test_gear_seed_quality_policy

#include "../ai/playerbot/policy/gear/EmptySlotFillPolicy.h"
#include "../runtime/GearSeedQualityPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    // The enum values the policy mirrors (ITEM_QUALITY_POOR = 0 is grey).
    CHECK(kSeedQualityNormal == 1);
    CHECK(kSeedQualityUncommon == 2);

    // Failing case before the fix: a level-7 band started at grey (0).
    CHECK(SeedQualityFloor(7) == kSeedQualityNormal);
    CHECK(SeedQualityFloor(7) != 0);

    // No level the seed can run at (5-19) includes grey.
    for (std::uint32_t level = 5; level < 20; ++level)
        CHECK(SeedQualityFloor(level) == kSeedQualityNormal);

    // From level 20 the band is unchanged: green floor.
    for (std::uint32_t level = 20; level <= 60; ++level)
        CHECK(SeedQualityFloor(level) == kSeedQualityUncommon);

    // Empty-slot backstop, fresh pool-bot seed at level 7: white is accepted, a slot with
    // only grey candidates stays empty.
    {
        ai::EmptySlotGates g;
        g.botLevel = 7;
        g.maxItemLevel = 8;
        g.minQuality = GearQualityFloor(7, true);
        g.maxQuality = 3;
        ai::EmptySlotCandidate grey;
        grey.itemId = 1; grey.itemLevel = 5; grey.requiredLevel = 5; grey.quality = 0; grey.equippable = true;
        ai::EmptySlotCandidate white = grey;
        white.itemId = 2; white.quality = 1;

        CHECK(g.minQuality == kSeedQualityNormal);
        std::vector<ai::EmptySlotCandidate> both{grey, white};
        auto ranked = ai::RankEmptySlotCandidates(g, both);
        CHECK(ranked.size() == 1 && both[ranked[0]].itemId == 2);
        std::vector<ai::EmptySlotCandidate> onlyGrey{grey};
        CHECK(ai::RankEmptySlotCandidates(g, onlyGrey).empty());

        // Player autogear, hired-companion provisioning and re-gearing are not
        // the fresh pool-bot seed (freshPoolSeed false): at level 7 the backstop
        // still accepts grey, so they can fill a slot with it.
        ai::EmptySlotGates legacy = g;
        legacy.minQuality = GearQualityFloor(7, false);
        CHECK(legacy.minQuality == 0);
        CHECK(ai::RankEmptySlotCandidates(legacy, onlyGrey).size() == 1);
    }
    // Non-seed floor is unchanged at 10-19 and 20+; the seed floor never drops below it.
    CHECK(GearQualityFloor(12, false) == kSeedQualityNormal);
    CHECK(GearQualityFloor(25, false) == kSeedQualityUncommon);
    for (std::uint32_t level = 5; level <= 60; ++level)
        CHECK(GearQualityFloor(level, true) == SeedQualityFloor(level));

    constexpr std::uint32_t atCompileTime = SeedQualityFloor(25);
    CHECK(atCompileTime == kSeedQualityUncommon);

    std::printf("test_gear_seed_quality_policy: %d checks passed\n", checks);
    return 0;
}
