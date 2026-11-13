// Standalone regression test for the c-cons class-consumable policy:
// soul-shard keep band, poison class mask, stone/oil upkeep classes, and
// the "keep until a higher tier is held" ladders.
// Server data, not guesses: 6265 Soul Shard (stack 3, keep 5), poisons
// allowable_class 8 (rogue mask), stones/oils class TRADE_GOODS unmasked.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_class_consumable_policy.cpp -o /tmp/test_class_consumable
//   /tmp/test_class_consumable

#include "../runtime/policy/provision/ClassConsumablePolicy.h"

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

// Rogue poisons: class mask 8, kept from the required level on until the
// bot holds a higher rank of the same poison - no level window. Crippling
// Poison (req 20) is the seed tier up to 49 and used to be sold from 27.
static void TestPoisonKeep()
{
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 20, false));
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 27, false));
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 49, false));
    CHECK(ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 60, false));
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 19, false));
    // A higher rank in the bags makes the lower one surplus.
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_ROGUE_ID, 8, 20, 50, true));
    // Wrong class never keeps a rogue-masked item.
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_WARLOCK_ID, 8, 20, 30, false));
    CHECK(!ShouldKeepClassMaskedConsumable(CLASS_MAGE_ID, 8, 20, 30, false));
}

// Tier ladders: higher tiers of the line an entry sits on, lowest first.
static void TestTierLadders()
{
    // Crippling Poison -> Crippling Poison II.
    ConsumableTiers t = HigherTiersOf(3775);
    CHECK(t.count == 1 && t.ids[0] == 3776);
    CHECK(HigherTiersOf(3776).count == 0);
    // Dense Sharpening Stone 12404 -> Elemental 18262, Consecrated 23122.
    t = HigherTiersOf(12404);
    CHECK(t.count == 2 && t.ids[0] == 18262 && t.ids[1] == 23122);
    // Dense Weightstone is the top weightstone; sharpening never counts for it.
    CHECK(HigherTiersOf(12643).count == 0);
    // Minor Wizard Oil -> Lesser, Wizard, Brilliant; mana oil is another line.
    t = HigherTiersOf(20744);
    CHECK(t.count == 3 && t.ids[0] == 20746);
    t = HigherTiersOf(20745);
    CHECK(t.count == 2 && t.ids[0] == 20747 && t.ids[1] == 20748);
    // Wound Poison I has three higher ranks; Instant Poison VI one listed.
    CHECK(HigherTiersOf(10918).count == 3);
    CHECK(HigherTiersOf(6947).count == 6);
    // Not on any ladder.
    CHECK(HigherTiersOf(13446).count == 0);
    CHECK(HigherTiersOf(0).count == 0);

    // HoldsHigherTier asks only about higher tiers of the same line.
    auto holdsCrippling2 = [](uint32_t id) { return id == 3776; };
    CHECK(HoldsHigherTier(3775, holdsCrippling2));
    CHECK(!HoldsHigherTier(3776, holdsCrippling2));
    CHECK(!HoldsHigherTier(6947, holdsCrippling2));
    auto holdsMinorWizard = [](uint32_t id) { return id == 20744; };
    CHECK(!HoldsHigherTier(20744, holdsMinorWizard));
    CHECK(!HoldsHigherTier(20745, holdsMinorWizard));
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

static void TestStoneOilKeep()
{
    CHECK(ShouldKeepStone(CLASS_WARRIOR_ID, 1, 5, false));
    // Dense stones (req 35) stay the seed tier to 60; the old +10 window sold them from 46.
    CHECK(ShouldKeepStone(CLASS_WARRIOR_ID, 35, 46, false));
    CHECK(ShouldKeepStone(CLASS_WARRIOR_ID, 35, 60, false));
    CHECK(!ShouldKeepStone(CLASS_WARRIOR_ID, 1, 12, true));
    CHECK(!ShouldKeepStone(CLASS_ROGUE_ID, 1, 5, false));
    CHECK(!ShouldKeepStone(CLASS_WARRIOR_ID, 15, 5, false));
    CHECK(ShouldKeepOil(CLASS_MAGE_ID, 5, 10, false));
    // Minor Wizard Oil (req 5) is the seed tier to 29; the old window sold it from 16.
    CHECK(ShouldKeepOil(CLASS_WARLOCK_ID, 5, 25, false));
    CHECK(!ShouldKeepOil(CLASS_MAGE_ID, 5, 30, true));
    CHECK(!ShouldKeepOil(CLASS_ROGUE_ID, 5, 10, false));
}

int main()
{
    TestShardBand();
    TestPoisonKeep();
    TestTierLadders();
    TestUpkeepClasses();
    TestStoneOilKeep();
    std::printf("class consumable policy: %d checks passed\n", checks);
    return 0;
}
