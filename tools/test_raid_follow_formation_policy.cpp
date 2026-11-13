// Raid/dungeon follow formation tests (section M of
// BOTS-PATHING-WAYPOINTS): the wedge keeps every follower behind the leader,
// large groups split over two rings, every formation refuses a spot over a
// drop, and a far follower walks the leader's own trail instead of cutting
// straight at it.
//
// Covers ai/RaidFollowWedgePolicy.h, ai/FollowSpotSafetyPolicy.h and
// ai/FollowTrailPolicy.h.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_raid_follow_formation_policy.cpp -o /tmp/test_raid_follow
//   /tmp/test_raid_follow

#include "../ai/playerbot/RaidFollowWedgePolicy.h"
#include "../ai/playerbot/FollowSpotSafetyPolicy.h"
#include "../ai/playerbot/FollowTrailPolicy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

// With the leader facing +x (orientation 0) every wedge slot has a negative
// x offset: nobody stands ahead of the leader, and no slot is even square
// beside it. Also checked for an arbitrary facing.
static void TestWedgeNeverInFront()
{
    for (int count = 1; count <= 40; ++count)
    {
        for (int index = 0; index < count; ++index)
        {
            float const angle = ai::RaidFollowWedgeAngle(0.0f, index, count);
            CHECK(std::cos(angle) < 0.0f);

            float const turned = ai::RaidFollowWedgeAngle(1.234f, index, count);
            float relative = turned - 1.234f - ai::kRaidFollowWedgePi;
            while (relative > ai::kRaidFollowWedgePi) relative -= 2.0f * ai::kRaidFollowWedgePi;
            while (relative < -ai::kRaidFollowWedgePi) relative += 2.0f * ai::kRaidFollowWedgePi;
            CHECK(std::fabs(relative) <= ai::kRaidFollowWedgeSpreadRad / 2.0f + 1e-4f);
        }
    }

    // The arc is symmetric behind the leader: the ends are 75 degrees off the
    // straight-back direction, and a single follower stands straight behind.
    CHECK(Near(ai::RaidFollowWedgeAngle(0.0f, 0, 20), ai::kRaidFollowWedgePi - ai::kRaidFollowWedgeSpreadRad / 2.0f));
    CHECK(Near(ai::RaidFollowWedgeAngle(0.0f, 19, 20), ai::kRaidFollowWedgePi + ai::kRaidFollowWedgeSpreadRad / 2.0f));
    CHECK(Near(ai::RaidFollowWedgeAngle(0.0f, 0, 1), ai::kRaidFollowWedgePi));
    CHECK(Near(ai::RaidFollowWedgeAngle(2.0f, 5, 1), 2.0f + ai::kRaidFollowWedgePi));
}

// Two rings only when the group is large, and the extra ring is for damage
// dealers: tank and healer slots stay on the inner ring.
static void TestWedgeRings()
{
    CHECK(ai::RaidFollowWedgeRingOffset(10, false, false) == 0.0f);
    CHECK(ai::RaidFollowWedgeRingOffset(11, false, false) == ai::kRaidFollowSecondRingOffsetYd);
    CHECK(ai::RaidFollowWedgeRingOffset(30, true, false) == 0.0f);
    CHECK(ai::RaidFollowWedgeRingOffset(30, false, true) == 0.0f);
    CHECK(ai::RaidFollowWedgeRingOffset(30, false, false) == ai::kRaidFollowSecondRingOffsetYd);
}

// Every formation's spot goes through the same ground rule: a point over a
// drop of more than four yards (the measured ledge cases are 8+ yd) is
// refused, a step of up to four yards is not.
static void TestDropRejected()
{
    CHECK(ai::FollowSpotGroundRejected(10.0f, 2.0f));   // 8 yd drop: the ledge
    CHECK(ai::FollowSpotGroundRejected(10.0f, 5.99f));  // a big step
    CHECK(!ai::FollowSpotGroundRejected(10.0f, 6.0f));  // exactly 4 yd: allowed
    CHECK(!ai::FollowSpotGroundRejected(10.0f, 9.0f));  // a normal stair step
    CHECK(!ai::FollowSpotGroundRejected(10.0f, 10.0f)); // level ground
    CHECK(!ai::FollowSpotGroundRejected(10.0f, 14.0f)); // up a step, clamp handles it
    CHECK(!ai::FollowSpotGroundRejected(10.0f, -100000.0f)); // missed floor
    CHECK(!ai::FollowSpotGroundRejected(10.0f, std::nanf("")));
    CHECK(!ai::FollowSpotGroundRejected(std::nanf(""), 2.0f));
}

static void Append(std::vector<ai::FollowTrailPoint>& trail, float x, float y, float z = 0.0f)
{
    trail.push_back(ai::FollowTrailPoint{ x, y, z });
}

// The measured failure: a follower far behind a leader that turned a corner
// must aim at a point on the recorded path before the corner, not straight at
// the leader (the straight line cuts across the corner).
static void TestFarFollowerWalksTheTrail()
{
    std::vector<ai::FollowTrailPoint> trail;
    for (int i = 0; i <= 10; ++i)
        Append(trail, 0.0f, 2.0f * i);       // north from (0,0) to (0,20)
    for (int i = 1; i <= 10; ++i)
        Append(trail, 2.0f * i, 20.0f);      // east from (2,20) to (20,20)

    // The follower stands 25 yd straight south of the trail's start; the
    // leader is at (20,20), so the direct line cuts diagonally across the
    // corner.
    int const index = ai::FollowTrailWaypoint(trail, -5.0f, 0.0f);
    CHECK(index >= 0);
    ai::FollowTrailPoint const& waypoint = trail[static_cast<std::size_t>(index)];
    CHECK(waypoint.x < 2.0f);                 // still on the first leg
    CHECK(waypoint.y >= 0.0f && waypoint.y <= 20.0f);
    CHECK(waypoint.x != 20.0f || waypoint.y != 20.0f); // never the leader's spot

    // The waypoint is a short step along the path from the nearest point.
    CHECK(ai::FollowTrailDistance2d(trail.front(), waypoint.x, waypoint.y) >=
          ai::kFollowTrailStepYd - 1e-3f);

    // An empty trail has no waypoint.
    std::vector<ai::FollowTrailPoint> empty;
    CHECK(ai::FollowTrailWaypoint(empty, 0.0f, 0.0f) == -1);
}

// The trail samples the leader's walk, ignores jitter, resets on a teleport
// and keeps its memory bounded.
static void TestTrailRecording()
{
    std::vector<ai::FollowTrailPoint> trail;
    CHECK(ai::FollowTrailRecord(trail, 0.0f, 0.0f, 0.0f));
    CHECK(trail.size() == 1);
    CHECK(!ai::FollowTrailRecord(trail, 0.5f, 0.0f, 0.0f)); // below the sample step
    CHECK(trail.size() == 1);
    CHECK(ai::FollowTrailRecord(trail, 3.0f, 0.0f, 0.0f));
    CHECK(trail.size() == 2);
    CHECK(ai::FollowTrailRecord(trail, 3.0f, 150.0f, 0.0f)); // teleport
    CHECK(trail.size() == 1);
    CHECK(Near(trail.back().y, 150.0f));

    for (int i = 0; i < 400; ++i)
        ai::FollowTrailRecord(trail, static_cast<float>(i) * 3.0f, 150.0f, 0.0f);
    CHECK(trail.size() <= ai::kFollowTrailMaxPoints);
    CHECK(trail.size() == ai::kFollowTrailMaxPoints);
}

// A back-off walks the trail backwards from the bot's place on it.
static void TestTrailBackOff()
{
    std::vector<ai::FollowTrailPoint> trail;
    for (int i = 0; i <= 20; ++i)
        trail.push_back({ static_cast<float>(i) * 2.0f, 0.0f, 0.0f });

    int nearest = -1;
    int const index = ai::FollowTrailBackOffIndex(trail, 30.0f, 1.0f, 10.0f, &nearest);
    CHECK(nearest == 15);
    CHECK(index == 10);

    // A short trail gives its start; nothing behind the start gives none.
    CHECK(ai::FollowTrailBackOffIndex(trail, 4.0f, 0.0f, 10.0f) == 0);
    CHECK(ai::FollowTrailBackOffIndex(trail, -3.0f, 0.0f, 10.0f) == -1);

    std::vector<ai::FollowTrailPoint> empty;
    CHECK(ai::FollowTrailBackOffIndex(empty, 0.0f, 0.0f, 10.0f) == -1);
}

// Entrance camp: the leader stands still, so only the bot's own walk in
// extends the trail, and the back-off then runs back along those steps.
static void TestTrailBackOffOnOwnSteps()
{
    std::vector<ai::FollowTrailPoint> trail;
    for (int i = 0; i <= 10; ++i)
        CHECK(ai::FollowTrailRecord(trail, 0.0f, 2.0f * static_cast<float>(i), 0.0f) || i == 0);

    int nearest = -1;
    CHECK(ai::FollowTrailBackOffIndex(trail, 0.0f, 0.0f, 8.0f, &nearest) == -1); // at the start: nothing behind
    CHECK(nearest == 0);

    int late = -1;
    CHECK(ai::FollowTrailBackOffIndex(trail, 0.0f, 20.0f, 8.0f, &late) == 6);
    CHECK(late == 10);
}

int main()
{
    TestTrailBackOff();
    TestTrailBackOffOnOwnSteps();
    TestWedgeNeverInFront();
    TestWedgeRings();
    TestDropRejected();
    TestFarFollowerWalksTheTrail();
    TestTrailRecording();
    std::printf("ok %d checks\n", checks);
    return 0;
}
