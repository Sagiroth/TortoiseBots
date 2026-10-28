// Standalone regression test for the summon point policy: where a summoned or
// newly hired bot lands next to its summoner.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_summon_point_policy.cpp -o /tmp/test_summon_point_policy
//   /tmp/test_summon_point_policy

#include "../behavior/SummonPointPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::SummonPointIsSafe;

int main()
{
    std::cout << "Starting TortoiseBots summon point policy tests...\n";

    // The rule used before this policy, for the failing-first case: it read
    // the z that UpdateGroundPositionZ left alone when no floor was found
    // (the anchor's own z) as "no drop", and the open-air line of sight
    // passed, so the point over the pit was taken.
    auto OldRule = [](float originZ, float destZ, bool los) {
        return std::fabs(destZ - originZ) <= 4.0f && los;
    };

    // Wailing Caverns, the Serpentis foot (z -58.4): the pit floor is 48 yd
    // down, outside the core's 10 yd height search, so no floor is found and
    // the old code kept destZ = originZ. The old rule says safe; it is the bug.
    CHECK(OldRule(-58.4f, -58.4f, true) == true);
    CHECK(SummonPointIsSafe(false, -58.4f, -58.4f, true) == false);
    std::cout << "  [PASS] no floor under the point within the height search is a drop\n";

    // A real floor 48 yd down: found, but a fall.
    CHECK(SummonPointIsSafe(true, -58.4f, -106.7f, true) == false);
    std::cout << "  [PASS] a floor far below the summoner is a drop\n";

    // A step 3 yd down with line of sight is fine.
    CHECK(SummonPointIsSafe(true, 0.0f, -3.0f, true) == true);
    std::cout << "  [PASS] a small step down with line of sight is used\n";

    // The 4 yd boundary is inclusive: 4.0 is the last good step, 4.01 is not.
    CHECK(SummonPointIsSafe(true, 0.0f, -4.0f, true) == true);
    CHECK(SummonPointIsSafe(true, 0.0f, -4.01f, true) == false);
    std::cout << "  [PASS] the drop limit is 4 yd inclusive\n";

    // No line of sight to the point: the summoner's exact spot instead.
    CHECK(SummonPointIsSafe(true, 0.0f, 0.0f, false) == false);
    std::cout << "  [PASS] a point the summoner cannot see is not used\n";

    std::cout << "All summon point policy checks PASSED!\n";
    return 0;
}
