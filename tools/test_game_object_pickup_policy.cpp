// Automatic game-object pickup: open world props holding only common items
// (Empty Festive Mug) stay on the ground unless a quest needs them; nodes,
// locked chests and open chests with better loot keep working. An explicit
// order (the Deadmines gunpowder chest, its common key) is always allowed.
//
//   g++ -std=c++17 -Wall -Wextra tools/test_game_object_pickup_policy.cpp -o /tmp/test_game_object_pickup

#include "../ai/playerbot/policy/loot/GameObjectPickupPolicy.h"

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

int main()
{
    GameObjectPickupFacts mug = OpenChest();
    mug.onlyCommonLoot = true;
    CHECK(!AllowGameObjectPickup(mug));

    GameObjectPickupFacts questMug = mug;
    questMug.questObject = true;
    CHECK(AllowGameObjectPickup(questMug));

    GameObjectPickupFacts treasure = OpenChest();
    CHECK(AllowGameObjectPickup(treasure));

    GameObjectPickupFacts node = mug;
    node.keyOrSkillLocked = true;
    CHECK(AllowGameObjectPickup(node));

    GameObjectPickupFacts notChest;
    notChest.onlyCommonLoot = true;
    CHECK(AllowGameObjectPickup(notChest));

    // The Deadmines gunpowder chest (GO 17155): a chest with an open lock, only common loot
    // (loot 2882 -> item 5397, class Key) and no quest link. A commander's loot order must
    // reach it; the bot's own pickup sweep still skips it.
    GameObjectPickupFacts orderedChest = OpenChest();
    orderedChest.onlyCommonLoot = true;
    orderedChest.playerOrder = true;
    CHECK(AllowGameObjectPickup(orderedChest));

    GameObjectPickupFacts orderedMug = mug;
    orderedMug.playerOrder = true;
    CHECK(AllowGameObjectPickup(orderedMug));

    // Same facts, no order: the filter still refuses it.
    GameObjectPickupFacts unorderedChest = orderedChest;
    unorderedChest.playerOrder = false;
    CHECK(!AllowGameObjectPickup(unorderedChest));

    std::printf("OK (%d checks)\n", checks);
    return 0;
}
