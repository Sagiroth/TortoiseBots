// Regression test for the decorative world-object rule:
// an unlocked single-item common-material chest with no quest link (Empty
// Festive Mug, GO 180905 -> item 21174) must not be picked up, while quest
// objects, gathering nodes, keyed/locked treasure and explicit player orders
// keep working. Pure policy header, no core or AI context needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_game_object_pickup_policy.cpp -o /tmp/test_game_object_pickup
//   /tmp/test_game_object_pickup

#include "../ai/playerbot/policy/loot/GameObjectPickupPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static GameObjectPickupFacts OpenChest()
{
    GameObjectPickupFacts facts;
    facts.isChest = true;
    return facts;
}

// The Festive Mug shape: open chest, one common item, no quest anywhere.
static void TestDecorativeMugRefused()
{
    GameObjectPickupFacts facts = OpenChest();
    facts.singleCommonItem = true;
    CHECK(!AllowGameObjectPickup(facts));
    CHECK(LootTableLooksDecorative({1}, false));
}

// A quest object is picked up even when its table is one common item
// (Furlbrow's Wardrobe 290 -> quest 64, Berard's Journal 1594 -> quest 450).
static void TestQuestObjectsAdmitted()
{
    GameObjectPickupFacts facts = OpenChest();
    facts.singleCommonItem = true;
    facts.questObject = true;
    CHECK(AllowGameObjectPickup(facts));
    CHECK(!LootTableLooksDecorative({1}, true));
}

// Gathering nodes keep working (Silverleaf 1617, Mageroyal 1620).
static void TestGatheringNodesAdmitted()
{
    GameObjectPickupFacts facts = OpenChest();
    facts.singleCommonItem = true;
    facts.gatheringNode = true;
    CHECK(AllowGameObjectPickup(facts));
}

// Keyed and lockpicked treasure keeps working (Secret Safe 161495,
// Battered Chest 2843, Dark Coffer 160845).
static void TestLockedTreasureAdmitted()
{
    GameObjectPickupFacts facts = OpenChest();
    facts.keyOrSkillLocked = true;
    CHECK(AllowGameObjectPickup(facts));

    GameObjectPickupFacts multiRow = OpenChest();
    CHECK(AllowGameObjectPickup(multiRow));
    CHECK(!LootTableLooksDecorative({1, 1, 2}, false));
}

// Multi-row tables never count as decorative (Dusty Tomes 179547/179548:
// 10-12 rows of skill books; real treasure: 100+ rows).
static void TestMultiRowTablesAdmitted()
{
    GameObjectPickupFacts facts = OpenChest();
    facts.singleCommonItem = false;
    CHECK(AllowGameObjectPickup(facts));
    CHECK(!LootTableLooksDecorative({}, false));
    CHECK(!LootTableLooksDecorative({1, 3}, false));
}

// Better-than-common loot is never decorative (recipes, books).
static void TestUncommonLootAdmitted()
{
    CHECK(!LootTableLooksDecorative({2}, false));
    CHECK(!LootTableLooksDecorative({3}, false));
}

// An explicit player order ("add all loot", farm order) always wins,
// even for a decorative mug.
static void TestPlayerOrderOverrides()
{
    GameObjectPickupFacts facts = OpenChest();
    facts.singleCommonItem = true;
    facts.orderedByPlayer = true;
    CHECK(AllowGameObjectPickup(facts));
}

// Non-chest objects (goobers, quest givers) are outside this rule.
static void TestNonChestUntouched()
{
    GameObjectPickupFacts facts;
    facts.singleCommonItem = true;
    CHECK(AllowGameObjectPickup(facts));
}

int main()
{
    std::printf("Starting game object pickup policy tests...\n");
    TestDecorativeMugRefused();
    std::printf("  [PASS] decorative mug refused\n");
    TestQuestObjectsAdmitted();
    std::printf("  [PASS] quest objects admitted\n");
    TestGatheringNodesAdmitted();
    std::printf("  [PASS] gathering nodes admitted\n");
    TestLockedTreasureAdmitted();
    std::printf("  [PASS] locked treasure admitted\n");
    TestMultiRowTablesAdmitted();
    std::printf("  [PASS] multi-row tables admitted\n");
    TestUncommonLootAdmitted();
    std::printf("  [PASS] uncommon loot admitted\n");
    TestPlayerOrderOverrides();
    std::printf("  [PASS] player order overrides\n");
    TestNonChestUntouched();
    std::printf("  [PASS] non-chest untouched\n");
    std::printf("All game object pickup policy checks PASSED (%d assertions)!\n", checks);
    return 0;
}
