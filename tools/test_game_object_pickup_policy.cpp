// Automatic game-object pickup: open world props (Empty Festive Mug) stay
// on the ground unless a quest needs them; nodes, locked chests and player
// orders keep working.
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
    CHECK(!AllowGameObjectPickup(OpenChest()));

    GameObjectPickupFacts quest = OpenChest();
    quest.questObject = true;
    CHECK(AllowGameObjectPickup(quest));

    GameObjectPickupFacts node = OpenChest();
    node.gatheringNode = true;
    CHECK(AllowGameObjectPickup(node));

    GameObjectPickupFacts locked = OpenChest();
    locked.keyOrSkillLocked = true;
    CHECK(AllowGameObjectPickup(locked));

    GameObjectPickupFacts ordered = OpenChest();
    ordered.orderedByPlayer = true;
    CHECK(AllowGameObjectPickup(ordered));

    GameObjectPickupFacts notChest;
    CHECK(AllowGameObjectPickup(notChest));

    std::printf("OK (%d checks)\n", checks);
    return 0;
}
