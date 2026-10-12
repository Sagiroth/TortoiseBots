#include "../ai/playerbot/RangedApproachPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ApproachPoint;
using ai::RangedApproachChoice;
using ai::SelectRangedApproachPoint;
using ai::UseRangedApproach;

static ApproachPoint P(float x, float y, float z = 0.0f)
{
    ApproachPoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    return point;
}

static float Dist(ApproachPoint const& a, ApproachPoint const& b)
{
    return std::sqrt(ai::ApproachDistanceSq(a, b));
}

static bool Near(ApproachPoint const& a, ApproachPoint const& b)
{
    return Dist(a, b) < 0.01f;
}

int main()
{
    std::cout << "Starting TortoiseBots ranged approach policy tests...\n";

    auto const seesAll = [](ApproachPoint const&) { return true; };

    // Only where the core chase fails: inside a dungeon, target in sight,
    // beyond melee reach. Open world, out of sight and melee keep the chase.
    {
        CHECK(UseRangedApproach(true, true, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(false, true, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(true, false, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(true, true, 5.0f, 5.0f));
        CHECK(!UseRangedApproach(true, true, 0.0f, 5.0f));
        std::cout << "  [PASS] gate: dungeon + in sight + beyond melee only\n";
    }

    // Uldaman shape: healer 38 yd from the tank down a straight corridor,
    // heal reach 27 yd. The point is on the path exactly 27 yd from the tank,
    // so the healer walks 11 yd and stops.
    {
        std::vector<ApproachPoint> path = { P(38, 0), P(30, 0), P(20, 0), P(10, 0), P(0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, seesAll);
        CHECK(choice.valid);
        CHECK(choice.inRange);
        CHECK(Near(choice.point, P(27, 0)));
        std::cout << "  [PASS] straight corridor: stops on the path at 27 yd\n";
    }

    // Path around a corner: the straight line would be inside a wall, the
    // chosen point is on the path segment and exactly `distance` from the target.
    {
        std::vector<ApproachPoint> path = { P(40, 30), P(40, 0), P(0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, seesAll);
        CHECK(choice.valid && choice.inRange);
        CHECK(std::fabs(choice.point.y) < 0.01f);
        CHECK(std::fabs(Dist(choice.point, P(0, 0)) - 25.0f) < 0.01f);
        std::cout << "  [PASS] corner: point on the last leg at range\n";
    }

    // The path re-enters range late; the walk back from the end stops where
    // the target goes out of sight (a pillar between point and target).
    {
        std::vector<ApproachPoint> path = { P(50, 0), P(20, 5), P(15, 0), P(10, 0), P(0, 0) };
        auto const pillar = [](ApproachPoint const& point) { return point.x < 18.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 30.0f, pillar);
        CHECK(choice.valid && choice.inRange);
        CHECK(Near(choice.point, P(15, 0)));
        std::cout << "  [PASS] line of sight: stops at the last point that sees the target\n";
    }

    // The exact-range point on the crossing segment is itself out of sight:
    // fall back to the in-range vertex that sees the target.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(10, 0), P(0, 0) };
        auto const near = [](ApproachPoint const& point) { return point.x <= 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, near);
        CHECK(choice.valid && choice.inRange);
        CHECK(Near(choice.point, P(10, 0)));
        std::cout << "  [PASS] edge point out of sight: keeps the vertex in sight\n";
    }

    // Range is 3D: a target on a dais 10 yd up counts its height.
    {
        std::vector<ApproachPoint> path = { P(40, 0, 0), P(0, 0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0, 10), 26.0f, seesAll);
        CHECK(choice.valid && choice.inRange);
        CHECK(std::fabs(Dist(choice.point, P(0, 0, 10)) - 26.0f) < 0.01f);
        CHECK(std::fabs(choice.point.x - 24.0f) < 0.01f);
        std::cout << "  [PASS] height counts towards the range\n";
    }

    // Partial path (navmesh island or seam) that ends out of range: walk to
    // its end when that closes the gap, never when it leads away.
    {
        std::vector<ApproachPoint> closer = { P(60, 0), P(45, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(closer, P(0, 0), 27.0f, seesAll);
        CHECK(choice.valid);
        CHECK(!choice.inRange);
        CHECK(Near(choice.point, P(45, 0)));

        std::vector<ApproachPoint> away = { P(40, 0), P(50, 10) };
        CHECK(!SelectRangedApproachPoint(away, P(0, 0), 27.0f, seesAll).valid);
        std::cout << "  [PASS] partial path: walks only towards the target\n";
    }

    // Nothing to do: already in range, too short a path, no distance.
    {
        std::vector<ApproachPoint> inRange = { P(20, 0), P(0, 0) };
        CHECK(!SelectRangedApproachPoint(inRange, P(0, 0), 27.0f, seesAll).valid);

        std::vector<ApproachPoint> single = { P(40, 0) };
        CHECK(!SelectRangedApproachPoint(single, P(0, 0), 27.0f, seesAll).valid);

        std::vector<ApproachPoint> path = { P(40, 0), P(0, 0) };
        CHECK(!SelectRangedApproachPoint(path, P(0, 0), 0.0f, seesAll).valid);
        std::cout << "  [PASS] degenerate inputs give no point\n";
    }

    // A complete path whose end does not see the target (target on a ledge
    // above the path's end): still the end, the closest the bot can stand.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(20, 0), P(5, 0) };
        auto const none = [](ApproachPoint const&) { return false; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, none);
        CHECK(choice.valid && choice.inRange);
        CHECK(Near(choice.point, P(5, 0)));
        std::cout << "  [PASS] no sight anywhere: the path end\n";
    }

    std::cout << "Ranged approach policy tests: OK\n";
    return 0;
}
