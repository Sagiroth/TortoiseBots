#include "../ai/playerbot/PullFiringPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ComputePullFiringPosition;
using ai::PullFiringPosition;

static float Distance(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

int main()
{
    std::cout << "Starting TortoiseBots pull firing-position policy tests...\n";

    // A ranged pull stands `firingDistance` from the target, on the puller's
    // own side of it: the walk is straight at the mob and never crosses it.
    // Issue #389: the old approach chased, and the core drops a chase in a
    // dungeon unless the destination is melee-reachable from the target.
    {
        PullFiringPosition const point = ComputePullFiringPosition(10.0f, 0.0f, 0.0f, 0.0f, 6.0f);
        CHECK(point.valid);
        CHECK(std::fabs(point.x - 6.0f) < 0.001f);
        CHECK(std::fabs(point.y - 0.0f) < 0.001f);
        CHECK(std::fabs(Distance(point.x, point.y, 0.0f, 0.0f) - 6.0f) < 0.001f);
        std::cout << "  [PASS] straight line: the point is 6 yd from the target\n";
    }

    // Off-axis: the point stays on the puller's ray, not merely at the right
    // distance, so the puller does not strafe around the mob.
    {
        PullFiringPosition const point = ComputePullFiringPosition(3.0f, 4.0f, 0.0f, 0.0f, 2.5f);
        CHECK(point.valid);
        CHECK(std::fabs(point.x - 1.5f) < 0.001f);
        CHECK(std::fabs(point.y - 2.0f) < 0.001f);
        CHECK(std::fabs(Distance(point.x, point.y, 0.0f, 0.0f) - 2.5f) < 0.001f);
        std::cout << "  [PASS] off-axis: the point stays on the puller's ray\n";
    }

    // The walk shrinks by exactly the range the puller overshot: the point is
    // between the puller and the target (firingDistance < puller distance),
    // never behind the puller.
    {
        PullFiringPosition const point = ComputePullFiringPosition(10.0f, 0.0f, 0.0f, 0.0f, 6.0f);
        CHECK(std::fabs(Distance(point.x, point.y, 10.0f, 0.0f) - 4.0f) < 0.001f);
        CHECK(std::fabs(Distance(point.x, point.y, 0.0f, 0.0f)) <
            std::fabs(Distance(10.0f, 0.0f, 0.0f, 0.0f)));
        std::cout << "  [PASS] the firing position lies between the puller and the target\n";
    }

    // Degenerate inputs have no firing position: the puller on top of the
    // target (no direction to walk along), and a non-positive distance.
    {
        CHECK(!ComputePullFiringPosition(0.0f, 0.0f, 0.0f, 0.0f, 6.0f).valid);
        CHECK(!ComputePullFiringPosition(5.0f, 5.0f, 0.0f, 0.0f, 0.0f).valid);
        CHECK(!ComputePullFiringPosition(5.0f, 5.0f, 0.0f, 0.0f, -1.0f).valid);
        std::cout << "  [PASS] degenerate inputs have no firing position\n";
    }

    // The firing distance is clamped to the puller's own distance: when the mob
    // is already closer than the firing range (out of sight around a corner,
    // say) the point is the puller's own position, never a point behind the
    // puller that would walk the tank backwards away from the mob.
    {
        PullFiringPosition const point = ComputePullFiringPosition(10.0f, 0.0f, 0.0f, 0.0f, 22.0f);
        CHECK(point.valid);
        CHECK(std::fabs(point.x - 10.0f) < 0.001f);
        CHECK(std::fabs(point.y - 0.0f) < 0.001f);
        std::cout << "  [PASS] firing distance is clamped to the puller's distance\n";
    }

    std::cout << "All pull firing-position policy checks PASSED!\n";
    return 0;
}
