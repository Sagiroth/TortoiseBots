#include "../ai/playerbot/DungeonFollowHoldPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FindFollowHoldPoint;
using ai::FollowHoldPack;
using ai::FollowHoldPoint;
using ai::FollowHoldResult;
using ai::kDungeonFollowHoldLeaderDistanceYd;
using ai::kDungeonFollowHoldMarginYd;
using ai::kDungeonFollowHoldZRangeYd;
using ai::UseDungeonFollowHold;

namespace
{
    float Dist(const FollowHoldPoint& p, const FollowHoldPack& c)
    {
        const float dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool Near(float a, float b, float eps = 0.01f)
    {
        return std::fabs(a - b) <= eps;
    }
}

int main()
{
    std::cout << "Starting TortoiseBots dungeon follow hold policy tests...\n";

    // Gate: option on, dungeon, nobody fighting, leader beyond 30 yd.
    CHECK(kDungeonFollowHoldLeaderDistanceYd == 30.0f);
    CHECK(UseDungeonFollowHold(true, true, false, false, 30.5f));
    CHECK(!UseDungeonFollowHold(false, true, false, false, 80.0f)); // option off (default)
    CHECK(!UseDungeonFollowHold(true, false, false, false, 80.0f)); // open world
    CHECK(!UseDungeonFollowHold(true, true, true, false, 80.0f));   // bot fighting
    CHECK(!UseDungeonFollowHold(true, true, false, true, 80.0f));   // leader fighting: go help
    CHECK(!UseDungeonFollowHold(true, true, false, false, 30.0f));  // leader close: core follow
    std::cout << "  [PASS] gate: dungeon, out of combat, leader far ahead, option on\n";

    const float floorZ = -48.7f;

    // Uldaman west room (navmesh route from the entrance north, pack at
    // (-274, 62)): an equal-level pack with a 20 yd aggro radius stops the
    // bot 3 yd outside that radius on the first leg, before the room.
    {
        const FollowHoldPoint route[] = {
            { -242.0f, 60.0f, floorZ },  // bot
            { -258.0f, 60.0f, floorZ },
            { -276.0f, 67.0f, floorZ },
            { -276.0f, 88.0f, floorZ },
            { -258.0f, 91.0f, floorZ },
        };
        const FollowHoldPack packs[] = { { -274.0f, 62.0f, floorZ, 20.0f, kDungeonFollowHoldZRangeYd } };
        FollowHoldResult r = FindFollowHoldPoint(route, 5, packs, 1, kDungeonFollowHoldMarginYd);
        CHECK(r.hold);
        CHECK(r.segment == 0);
        CHECK(r.pack == 0);
        CHECK(Near(Dist(r.point, packs[0]), 20.0f + kDungeonFollowHoldMarginYd));
        CHECK(r.point.x < -242.0f && r.point.x > -258.0f); // on the first leg
    }
    std::cout << "  [PASS] Uldaman west-room route holds outside the aggro radius\n";

    // A pack well off the route does not stop the bot.
    {
        const FollowHoldPoint route[] = { { 0.0f, 0.0f, 0.0f }, { 40.0f, 0.0f, 0.0f }, { 80.0f, 0.0f, 0.0f } };
        const FollowHoldPack packs[] = { { 40.0f, 30.0f, 0.0f, 20.0f, kDungeonFollowHoldZRangeYd } };
        CHECK(!FindFollowHoldPoint(route, 3, packs, 1, kDungeonFollowHoldMarginYd).hold);
    }
    std::cout << "  [PASS] pack off the route is ignored\n";

    // The hold lands on the segment where the route first enters a radius,
    // and that segment's index maps back to the route.
    {
        const FollowHoldPoint route[] = { { 0.0f, 0.0f, 0.0f }, { 30.0f, 0.0f, 0.0f }, { 30.0f, 60.0f, 0.0f } };
        const FollowHoldPack packs[] = { { 30.0f, 50.0f, 0.0f, 15.0f, kDungeonFollowHoldZRangeYd } };
        FollowHoldResult r = FindFollowHoldPoint(route, 3, packs, 1, 3.0f);
        CHECK(r.hold);
        CHECK(r.segment == 1);
        CHECK(Near(r.point.x, 30.0f));
        CHECK(Near(r.point.y, 50.0f - 18.0f));
    }
    std::cout << "  [PASS] hold on a later leg, at radius + margin\n";

    // The pack the route reaches first wins, not the first in the list.
    {
        const FollowHoldPoint route[] = { { 0.0f, 0.0f, 0.0f }, { 100.0f, 0.0f, 0.0f } };
        const FollowHoldPack packs[] = {
            { 80.0f, 0.0f, 0.0f, 10.0f, kDungeonFollowHoldZRangeYd },
            { 40.0f, 5.0f, 0.0f, 10.0f, kDungeonFollowHoldZRangeYd },
        };
        FollowHoldResult r = FindFollowHoldPoint(route, 2, packs, 2, 3.0f);
        CHECK(r.hold);
        CHECK(r.pack == 1);
        CHECK(Near(Dist(r.point, packs[1]), 13.0f));
    }
    std::cout << "  [PASS] nearest pack along the route wins\n";

    // A creature on another floor (more than 3 yd above or below the whole
    // leg) does not aggro; a flying one (zRange 0) does.
    {
        const FollowHoldPoint route[] = { { 0.0f, 0.0f, 0.0f }, { 60.0f, 0.0f, 0.0f } };
        const FollowHoldPack below[] = { { 30.0f, 0.0f, -8.0f, 20.0f, kDungeonFollowHoldZRangeYd } };
        CHECK(!FindFollowHoldPoint(route, 2, below, 1, 3.0f).hold);
        const FollowHoldPack flyer[] = { { 30.0f, 0.0f, -8.0f, 20.0f, 0.0f } };
        CHECK(FindFollowHoldPoint(route, 2, flyer, 1, 3.0f).hold);
    }
    std::cout << "  [PASS] other floor ignored, flyers counted\n";

    // Standing on the hold point (inside radius + margin): a route that goes
    // further in holds right here; a route that leads away walks on.
    {
        const FollowHoldPack packs[] = { { 0.0f, 0.0f, 0.0f, 20.0f, kDungeonFollowHoldZRangeYd } };
        const FollowHoldPoint inward[] = { { 22.0f, 0.0f, 0.0f }, { 0.0f, 30.0f, 0.0f } };
        FollowHoldResult r = FindFollowHoldPoint(inward, 2, packs, 1, 3.0f);
        CHECK(r.hold);
        CHECK(r.segment == 0);
        CHECK(Near(r.point.x, 22.0f) && Near(r.point.y, 0.0f));

        const FollowHoldPoint outward[] = { { 22.0f, 0.0f, 0.0f }, { 60.0f, 0.0f, 0.0f } };
        CHECK(!FindFollowHoldPoint(outward, 2, packs, 1, 3.0f).hold);
    }
    std::cout << "  [PASS] already at the hold point: hold inward, free outward\n";

    // Degenerate input never holds.
    {
        const FollowHoldPoint one[] = { { 0.0f, 0.0f, 0.0f } };
        const FollowHoldPack packs[] = { { 0.0f, 0.0f, 0.0f, 20.0f, kDungeonFollowHoldZRangeYd } };
        CHECK(!FindFollowHoldPoint(one, 1, packs, 1, 3.0f).hold);
        const FollowHoldPoint route[] = { { 0.0f, 0.0f, 0.0f }, { 60.0f, 0.0f, 0.0f } };
        const FollowHoldPack none[] = { { 30.0f, 0.0f, 0.0f, 0.0f, kDungeonFollowHoldZRangeYd } };
        CHECK(!FindFollowHoldPoint(route, 2, none, 1, 3.0f).hold);
        CHECK(!FindFollowHoldPoint(route, 2, packs, 0, 3.0f).hold);
        const FollowHoldPoint still[] = { { 30.0f, 0.0f, 0.0f }, { 30.0f, 0.0f, 0.0f } };
        CHECK(!FindFollowHoldPoint(still, 2, packs, 1, 3.0f).hold);
    }
    std::cout << "  [PASS] degenerate routes and packs never hold\n";

    std::cout << "All dungeon follow hold policy tests passed.\n";
    return 0;
}
