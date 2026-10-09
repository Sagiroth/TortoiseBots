#include "../ai/playerbot/WorkIdlePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kWorkIdleReleaseSec;
using ai::WorkIdleAnchor;
using ai::WorkIdleAnchorKey;
using ai::WorkIdleStale;

int main()
{
    std::cout << "Starting TortoiseBots work-idle release tests...\n";

    // The horizon is ~30 s: three pool visits at the ~10 s cadence, so one
    // empty read cannot drop a trip whose prey is still spawning.
    CHECK(kWorkIdleReleaseSec == 30);
    CHECK(WorkIdleAnchorKey() == std::string("work idle since"));
    std::cout << "  [PASS] horizon and anchor key are fixed\n";

    // No anchor yet: never stale, whatever the scene says.
    CHECK(!WorkIdleStale(0, 1000, false, false));
    CHECK(!WorkIdleStale(0, 1000, true, true));
    std::cout << "  [PASS] unanchored stay never releases\n";

    // Inside the horizon: a fresh arrival holds even with nothing to do.
    CHECK(!WorkIdleStale(1000, 1000 + kWorkIdleReleaseSec - 1, false, false));
    CHECK(!WorkIdleStale(1000, 1000, false, false));
    std::cout << "  [PASS] fresh arrival holds through the horizon\n";

    // Past the horizon with nothing to do: release, so the next visit
    // requests a new target instead of standing out the 5-min WORK clock.
    CHECK(WorkIdleStale(1000, 1000 + kWorkIdleReleaseSec, false, false));
    CHECK(WorkIdleStale(1000, 1000 + 300, false, false));
    std::cout << "  [PASS] stale empty stay releases\n";

    // Either signal showing work vetoes the release: a prey to kill, or a
    // fight already on. Progress re-arms instead of expiring mid-fight.
    CHECK(!WorkIdleStale(1000, 1000 + kWorkIdleReleaseSec, true, false));
    CHECK(!WorkIdleStale(1000, 1000 + kWorkIdleReleaseSec, false, true));
    CHECK(!WorkIdleStale(1000, 1000 + 300, true, true));
    std::cout << "  [PASS] prey or attackers hold the stay\n";

    // Anchor upkeep: starts on the first empty tick, persists while empty,
    // clears the moment either signal shows work.
    CHECK(WorkIdleAnchor(0, 1000, false, false) == 1000);
    CHECK(WorkIdleAnchor(1000, 1010, false, false) == 1000);
    CHECK(WorkIdleAnchor(1000, 1010, true, false) == 0);
    CHECK(WorkIdleAnchor(1000, 1010, false, true) == 0);
    CHECK(WorkIdleAnchor(0, 1000, true, true) == 0);
    std::cout << "  [PASS] anchor starts, persists, and clears on work\n";

    std::cout << "All work-idle release tests passed.\n";
    return 0;
}
