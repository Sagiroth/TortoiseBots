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

    // Only where the core chase fails: inside a dungeon, beyond melee reach.
    // Open world and melee keep the chase (the caller checks sight after this).
    {
        CHECK(UseRangedApproach(true, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(false, 30.0f, 5.0f));
        CHECK(!UseRangedApproach(true, 5.0f, 5.0f));
        CHECK(!UseRangedApproach(true, 0.0f, 5.0f));
        std::cout << "  [PASS] gate: dungeon + beyond melee only\n";
    }

    // Uldaman shape: healer 38 yd from the tank down a straight corridor,
    // heal reach 27 yd. The point is on the path exactly 27 yd from the tank,
    // so the healer walks 11 yd and stops.
    {
        std::vector<ApproachPoint> path = { P(38, 0), P(30, 0), P(20, 0), P(10, 0), P(0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, seesAll);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(27, 0)));
        std::cout << "  [PASS] straight corridor: stops on the path at 27 yd\n";
    }

    // Path around a corner: the straight line would be inside a wall, the
    // chosen point is on the path segment and exactly `distance` from the target.
    {
        std::vector<ApproachPoint> path = { P(40, 30), P(40, 0), P(0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, seesAll);
        CHECK(choice.valid);
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
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(15, 0)));
        std::cout << "  [PASS] line of sight: stops at the last point that sees the target\n";
    }

    // The exact-range point on the crossing segment is itself out of sight:
    // fall back to the in-range vertex that sees the target.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(10, 0), P(0, 0) };
        auto const near = [](ApproachPoint const& point) { return point.x <= 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, near);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(10, 0)));
        std::cout << "  [PASS] edge point out of sight: keeps the vertex in sight\n";
    }

    // Range is 3D: a target on a dais 10 yd up counts its height.
    {
        std::vector<ApproachPoint> path = { P(40, 0, 0), P(0, 0, 0) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0, 10), 26.0f, seesAll);
        CHECK(choice.valid);
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
        CHECK(Near(choice.point, P(45, 0)));

        std::vector<ApproachPoint> away = { P(40, 0), P(50, 10) };
        CHECK(!SelectRangedApproachPoint(away, P(0, 0), 27.0f, seesAll).valid);
        std::cout << "  [PASS] partial path: walks only towards the target\n";
    }

    // Nothing to do: already in range, too short a path, no distance.
    {
        std::vector<ApproachPoint> alreadyThere = { P(20, 0), P(0, 0) };
        CHECK(!SelectRangedApproachPoint(alreadyThere, P(0, 0), 27.0f, seesAll).valid);

        std::vector<ApproachPoint> single = { P(40, 0) };
        CHECK(!SelectRangedApproachPoint(single, P(0, 0), 27.0f, seesAll).valid);

        std::vector<ApproachPoint> path = { P(40, 0), P(0, 0) };
        CHECK(!SelectRangedApproachPoint(path, P(0, 0), 0.0f, seesAll).valid);
        std::cout << "  [PASS] degenerate inputs give no point\n";
    }

    // Partial path whose end is in range but hidden (the target on a dais,
    // the path ending under its lip): never park the bot out of sight. The
    // anchor is the last point that still sees the target, then the usual
    // walk back and exact-range point from there. The bot itself sees the
    // target (the caller's gate); only the end of the path does not.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(30, 0), P(20, 0), P(5, 0) };
        auto const hiddenEnd = [](ApproachPoint const& point) { return point.x > 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, hiddenEnd);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(27, 0)));
        CHECK(hiddenEnd(choice.point));
        std::cout << "  [PASS] partial path, end in range but hidden: last stretch in sight\n";
    }

    // Same, with only the hidden end inside range: the bot walks to the
    // closest point that still sees the target, even though that is short of
    // range, instead of parking under the dais.
    {
        std::vector<ApproachPoint> path = { P(40, 0), P(32, 0), P(5, 0) };
        auto const hiddenEnd = [](ApproachPoint const& point) { return point.x > 10.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, hiddenEnd);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(32, 0)));
        std::cout << "  [PASS] partial path, only the hidden end in range: closest point in sight\n";
    }

    // Partial path whose end is out of range and hidden: a closer point in
    // sight if there is one; none closer than the bot gives no choice, and the
    // caller falls back to the chase.
    {
        std::vector<ApproachPoint> path = { P(60, 0), P(50, 0), P(45, 0) };
        auto const hiddenEnd = [](ApproachPoint const& point) { return point.x > 47.0f; };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 27.0f, hiddenEnd);
        CHECK(choice.valid);
        CHECK(Near(choice.point, P(50, 0)));

        std::vector<ApproachPoint> onlyEnd = { P(60, 0), P(45, 0) };
        auto const botOnly = [](ApproachPoint const& point) { return point.x > 55.0f; };
        CHECK(!SelectRangedApproachPoint(onlyEnd, P(0, 0), 27.0f, botOnly).valid);
        std::cout << "  [PASS] partial path, end out of range and hidden: closer point in sight or none\n";
    }

    // Near-tangent crossing: the segment grazes the range sphere, so the
    // discriminant is tiny next to b*b and float error can push it below
    // zero. Clamp, don't bail: the point lands on the sphere, not the vertex.
    {
        std::vector<ApproachPoint> path = { P(80, 30), P(60, 25), P(0, 25) };
        RangedApproachChoice const choice = SelectRangedApproachPoint(path, P(0, 0), 25.0f, seesAll);
        CHECK(choice.valid);
        CHECK(std::fabs(Dist(choice.point, P(0, 0)) - 25.0f) < 0.05f);
        CHECK(choice.point.x > -1.0f && choice.point.x < 1.0f);
        std::cout << "  [PASS] near-tangent crossing: clamps, keeps the touch point\n";
    }


    std::cout << "Ranged approach policy tests: OK\n";
    return 0;
}
