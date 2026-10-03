// Standalone regression test for the c-cons class-consumable policy:
// soul-shard keep band, poison class-mask window, stone/oil upkeep classes.
// Server data, not guesses: 6265 Soul Shard (stack 3, keep 5), poisons
// allowable_class 8 (rogue mask), stones/oils class TRADE_GOODS unmasked.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_class_consumable_policy.cpp -o /tmp/test_class_consumable
//   /tmp/test_class_consumable

#include "../runtime/ClassConsumablePolicy.h"

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

// Drain below the keep band of 5, destroy above it.
static void TestShardBand()
{
    CHECK(SOUL_SHARD_ITEM_ID == 6265);
    CHECK(SOUL_SHARD_KEEP_COUNT == 5);
    CHECK(ShouldDrainSoulForShards(0));
    CHECK(ShouldDrainSoulForShards(4));
    CHECK(!ShouldDrainSoulForShards(5));
    CHECK(!ShouldDrainSoulForShards(6));
    CHECK(SoulShardOverflow(5) == 0);
    CHECK(SoulShardOverflow(6) == 1);
    CHECK(SoulShardOverflow(10) == 5);
    CHECK(DRAIN_SOUL_TARGET_HEALTH_PCT == 20);
}

// Rogue poisons: class mask 8, req 20, stale past +6 (rank II at 28).
static void TestPoisonWindow()
{
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 20));
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 26));
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 27));
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 19));
    // Wrong class never keeps a rogue-masked item.
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_WARLOCK_ID, 8, 20, 30));
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_MAGE_ID, 8, 20, 30));
    // Deadly I (req 30) live at 30, stale by 37.
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 30, 30));
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 30, 37));
}

// Stones: warrior/paladin/druid only; oils: everyone but warrior/rogue.
static void TestUpkeepClasses()
{
    CHECK(UsesStones(CLASS_WARRIOR_ID));
    CHECK(UsesStones(CLASS_PALADIN_ID));
    CHECK(UsesStones(CLASS_DRUID_ID));
    CHECK(!UsesStones(CLASS_ROGUE_ID));
    CHECK(!UsesStones(CLASS_HUNTER_ID));
    CHECK(!UsesStones(CLASS_MAGE_ID));
    CHECK(!UsesStones(CLASS_WARLOCK_ID));

    CHECK(UsesOils(CLASS_MAGE_ID));
    CHECK(UsesOils(CLASS_WARLOCK_ID));
    CHECK(UsesOils(CLASS_PRIEST_ID));
    CHECK(UsesOils(CLASS_PALADIN_ID));
    CHECK(UsesOils(CLASS_HUNTER_ID));
    CHECK(!UsesOils(CLASS_WARRIOR_ID));
    CHECK(!UsesOils(CLASS_ROGUE_ID));

    CHECK(IsStoneEntry(2862));
    CHECK(IsStoneEntry(3239));
    CHECK(IsStoneEntry(12404));
    CHECK(!IsStoneEntry(6947));
    CHECK(IsOilEntry(20744));
    CHECK(IsOilEntry(20748));
    CHECK(!IsOilEntry(2862));
}

static void TestStoneOilWindow()
{
    CHECK(ShouldKeepStone(CLASS_WARRIOR_ID, 1, 5));
    CHECK(!ShouldKeepStone(CLASS_WARRIOR_ID, 1, 12));
    CHECK(!ShouldKeepStone(CLASS_ROGUE_ID, 1, 5));
    CHECK(!ShouldKeepStone(CLASS_WARRIOR_ID, 15, 5));
    CHECK(ShouldKeepOil(CLASS_MAGE_ID, 5, 10));
    CHECK(!ShouldKeepOil(CLASS_MAGE_ID, 5, 30));
    CHECK(!ShouldKeepOil(CLASS_ROGUE_ID, 5, 10));
}

int main()
{
    TestShardBand();
    TestPoisonWindow();
    TestUpkeepClasses();
    TestStoneOilWindow();
    std::printf("class consumable policy: %d checks passed\n", checks);
    return 0;
}
