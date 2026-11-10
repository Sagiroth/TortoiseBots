// A land walk launches only on a real navmesh path: NOPATH, shortcuts and
// not-using-path never move, and an INCOMPLETE path only when its actual end
// is meaningfully nearer the target than the start was.
// Mirrors ai/playerbot/LandWalkPolicy.h (no server headers needed).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_land_walk_policy.cpp -o /tmp/test_land_walk_policy && /tmp/test_land_walk_policy

#include "../ai/playerbot/LandWalkPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
    // Bot at the origin walking to (10, 0, 0).
    constexpr float kToX = 10.0f;
    constexpr std::uint32_t kNormal = kLandPathNormal;
    constexpr std::uint32_t kIncomplete = kLandPathIncomplete;
}

static void TestNormalWalks()
{
    CHECK(ClassifyLandPath(kNormal, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Walk);
}

static void TestShortcutAndNoPathRefused()
{
    CHECK(ClassifyLandPath(kLandPathNoPath, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Refuse);
    CHECK(ClassifyLandPath(kLandPathShortcut, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Refuse);
    // The core's shortcut line to an unreachable target: BuildShortcut with
    // the start as the path and NOPATH set (NOPATH wins over a NORMAL bit).
    CHECK(ClassifyLandPath(kLandPathNoPath | kLandPathNormal, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f,
        kToX, 0.0f, 0.0f) == LandWalkVerdict::Refuse);
    // Not-using-path on land (no mmaps under the destination) is the same
    // straight line with a different bit.
    CHECK(ClassifyLandPath(kLandPathNotUsingPath | kLandPathNormal, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f,
        kToX, 0.0f, 0.0f) == LandWalkVerdict::Refuse);
    // An unknown answer (none of the bits the helper knows) is not a walk.
    CHECK(ClassifyLandPath(0, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Refuse);
}

static void TestIncompleteNeedsProgress()
{
    // Halfway there: closes 5 yd, walks.
    CHECK(ClassifyLandPath(kIncomplete, 0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Walk);
    // A sidestep along a wall: same distance, refused.
    CHECK(ClassifyLandPath(kIncomplete, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Refuse);
    // Short of the 2 yd bar: refused.
    CHECK(ClassifyLandPath(kIncomplete, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Refuse);
    // On the 2 yd bar: walks.
    CHECK(ClassifyLandPath(kIncomplete, 0.0f, 0.0f, 0.0f, 8.0f, 0.0f, 0.0f, kToX, 0.0f, 0.0f) ==
        LandWalkVerdict::Walk);
}

static void TestHazardLegReach()
{
    CHECK(HazardLegReaches(true, 1.0f, 0.5f));
    CHECK(!HazardLegReaches(false, 1.0f, 0.5f));
    CHECK(!HazardLegReaches(true, 4.0f, 0.5f));
    CHECK(!HazardLegReaches(true, 1.0f, 2.5f));
    CHECK(HazardLegReaches(true, 0.0f, 0.0f));
    CHECK(!HazardLegReaches(true, 4.0f, 0.0f));
}

int main()
{
    TestNormalWalks();
    TestShortcutAndNoPathRefused();
    TestIncompleteNeedsProgress();
    TestHazardLegReach();
    std::cout << "test_land_walk_policy OK\n";
    return 0;
}
