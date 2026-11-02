// Standalone test for the fresh-seed quality floor (issue #1485, F2): a bot at
// levels 5-19 is seeded from white upwards (no grey), and from level 20 the band
// is unchanged (green and up).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_gear_seed_quality_policy.cpp -o /tmp/test_gear_seed_quality_policy
//   /tmp/test_gear_seed_quality_policy

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

    constexpr std::uint32_t atCompileTime = SeedQualityFloor(25);
    CHECK(atCompileTime == kSeedQualityUncommon);

    std::printf("test_gear_seed_quality_policy: %d checks passed\n", checks);
    return 0;
}
