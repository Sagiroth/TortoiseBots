#include "../ai/playerbot/policy/combat/EvadeChasePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::EvadeChaseState;
using ai::kEvadeChaseHomeLeashYd;
using ai::ShouldDropDraggedTarget;
using ai::ShouldNotChaseEvader;

int main()
{
    std::cout << "Starting TortoiseBots evade-chase gate tests...\n";

    // Never chase an evader: it is walking home invulnerable. A mob the
    // core merely flags unreachable is still damaging, so the reach keeps
    // the order and falls through to the movement and the give-up.
    CHECK(ShouldNotChaseEvader(true));
    CHECK(!ShouldNotChaseEvader(false));
    std::cout << "  [PASS] evading targets are not chased\n";

    // Drop only past the leash: a brief evade flicker at the pull keeps
    // the order, a dragged fight does not.
    {
        EvadeChaseState near;
        near.evading = true;
        near.homeDistanceYd = 5.0f;
        near.combatStartDistanceYd = 3.0f;
        CHECK(!ShouldDropDraggedTarget(near));

        EvadeChaseState draggedHome;
        draggedHome.evading = true;
        draggedHome.homeDistanceYd = kEvadeChaseHomeLeashYd + 1.0f;
        CHECK(ShouldDropDraggedTarget(draggedHome));

        // Unreachable-only past the leash keeps the order: the mob is still
        // damaging, and dropping it left the bot standing in combat without
        // a swing. The 15 s give-up below is its backstop.
        EvadeChaseState draggedStart;
        draggedStart.unreachable = true;
        draggedStart.homeDistanceYd = 5.0f;
        draggedStart.combatStartDistanceYd = kEvadeChaseHomeLeashYd + 10.0f;
        CHECK(!ShouldDropDraggedTarget(draggedStart));

        // At exactly the leash: still held, strictly-past drops.
        EvadeChaseState edge;
        edge.evading = true;
        edge.homeDistanceYd = kEvadeChaseHomeLeashYd;
        edge.combatStartDistanceYd = kEvadeChaseHomeLeashYd;
        CHECK(!ShouldDropDraggedTarget(edge));

        // No evade flags, far from home (a runner, not a reset): never drop.
        EvadeChaseState runner;
        runner.homeDistanceYd = 200.0f;
        runner.combatStartDistanceYd = 150.0f;
        CHECK(!ShouldDropDraggedTarget(runner));
        std::cout << "  [PASS] dragged-past-the-leash orders are dropped\n";
    }

    std::cout << "All evade-chase gate checks PASSED!\n";
    return 0;
}
