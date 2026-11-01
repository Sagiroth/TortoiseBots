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

    // Never chase: evading or unreachable, either one alone is enough.
    CHECK(ShouldNotChaseEvader(true, false));
    CHECK(ShouldNotChaseEvader(false, true));
    CHECK(ShouldNotChaseEvader(true, true));
    CHECK(!ShouldNotChaseEvader(false, false));
    std::cout << "  [PASS] evading or unreachable targets are not chased\n";

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

        EvadeChaseState draggedStart;
        draggedStart.unreachable = true;
        draggedStart.homeDistanceYd = 5.0f;
        draggedStart.combatStartDistanceYd = kEvadeChaseHomeLeashYd + 10.0f;
        CHECK(ShouldDropDraggedTarget(draggedStart));

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
