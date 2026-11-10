// RouteReplayPolicy.h: the replay asks the navmesh from the bot to points
// ahead (longest stretch first), walks the mesh answer only when it reaches
// a goal without an absurd detour, keeps the stored stretch otherwise.
// Mirrors ai/playerbot/RouteReplayPolicy.h (no server headers needed).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_route_replay_policy.cpp -o /tmp/test_route_replay_policy && /tmp/test_route_replay_policy

#include "../ai/playerbot/RouteReplayPolicy.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
    RouteReplayProbe Reached(float meshYd, float storedYd, bool last)
    {
        RouteReplayProbe probe;
        probe.reachable = true;
        probe.meshLengthYd = meshYd;
        probe.storedLengthYd = storedYd;
        probe.lastPoint = last;
        return probe;
    }
}

static void TestFullStretchWalks()
{
    // The common case: the mesh reaches the leg's last point on a sane line.
    CHECK(RouteReplayAcceptsProbe(Reached(120.0f, 110.0f, true)));
    CHECK(RouteReplayAcceptsProbe(Reached(110.0f, 110.0f, true)));
}

static void TestDetourHoldsTheStoredLine()
{
    // The mesh goes around something the stored route crosses (a jump down,
    // a gap): hold the stored stretch instead of the detour.
    CHECK(!RouteReplayAcceptsProbe(Reached(400.0f, 100.0f, true)));
    CHECK(!RouteReplayAcceptsProbe(Reached(3.0f * 100.0f + 1.0f, 100.0f, true)));
    // Exactly the ratio still walks: a player walks around.
    CHECK(RouteReplayAcceptsProbe(Reached(3.0f * 100.0f, 100.0f, true)));
}

static void TestUnreachableNeverWalks()
{
    RouteReplayProbe probe;
    CHECK(!RouteReplayAcceptsProbe(probe));
    probe = Reached(50.0f, 100.0f, false);
    probe.reachable = false;
    CHECK(!RouteReplayAcceptsProbe(probe));
}

static void TestDegenerateLengthsNeverWalk()
{
    CHECK(!RouteReplayAcceptsProbe(Reached(0.0f, 100.0f, true)));
    CHECK(!RouteReplayAcceptsProbe(Reached(50.0f, 0.0f, true)));
    CHECK(!RouteReplayAcceptsProbe(Reached(-5.0f, 100.0f, true)));
}

static void TestGoalsHalveTowardTheHead()
{
    // Eight points in the leg: last, then halved toward the head; the head
    // point itself (index 0) is never asked.
    std::vector<std::size_t> const goals = RouteReplayGoals(8);
    CHECK(goals.size() == 3);
    CHECK(goals[0] == 7);
    CHECK(goals[1] == 3);
    CHECK(goals[2] == 1);
    // Few points: only the last is asked, and only past the head.
    std::vector<std::size_t> two = RouteReplayGoals(2);
    CHECK(two.size() == 1);
    CHECK(two[0] == 1);
    CHECK(RouteReplayGoals(1).empty());
    CHECK(RouteReplayGoals(0).empty());
}

static void TestReplaySkipsWaterAndRewrites()
{
    CHECK(RouteReplayApplies(false, false));
    CHECK(!RouteReplayApplies(true, false)); // hazard rewrite keeps its shape
    CHECK(!RouteReplayApplies(false, true)); // water legs keep today's path
    CHECK(!RouteReplayApplies(true, true));
}

int main()
{
    std::cout << "Starting TortoiseBots route replay policy tests...\n";

    TestFullStretchWalks();
    std::cout << "  [PASS] full stretch walks\n";
    TestDetourHoldsTheStoredLine();
    std::cout << "  [PASS] detour holds the stored line\n";
    TestUnreachableNeverWalks();
    std::cout << "  [PASS] unreachable never walks\n";
    TestDegenerateLengthsNeverWalk();
    std::cout << "  [PASS] degenerate lengths never walk\n";
    TestGoalsHalveTowardTheHead();
    std::cout << "  [PASS] goals halve toward the head\n";
    TestReplaySkipsWaterAndRewrites();
    std::cout << "  [PASS] replay skips water and rewrites\n";

    std::cout << "All TortoiseBots route replay policy tests passed.\n";
    return 0;
}
