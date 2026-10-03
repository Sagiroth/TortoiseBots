#include "../ai/playerbot/GhostStallPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ShouldGhostStallTeleport;
using ai::ShouldKeepRidingCorpseRun;

// Core MotionMaster.h: IDLE_MOTION_TYPE = 0, POINT_MOTION_TYPE = 8.
int const IDLE = 0;
int const POINT = 8;

int main()
{
    std::cout << "Starting TortoiseBots ghost-stall policy tests...\n";

    // 1. The stall signature: a POINT generator already in flight toward
    // (nearly) the same destination keeps riding, no re-dispatch.
    CHECK(ShouldKeepRidingCorpseRun(false, true, POINT, POINT, 0.0f));
    CHECK(ShouldKeepRidingCorpseRun(false, true, POINT, POINT, 10.0f));
    std::cout << "  [PASS] in-flight POINT spline keeps riding\n";

    // 2. The launch tick is not a stall: the POINT generator appeared from
    // THIS tick's dispatch (before == IDLE), so there is nothing to keep.
    CHECK(!ShouldKeepRidingCorpseRun(false, true, IDLE, POINT, 0.0f));
    std::cout << "  [PASS] launch tick (IDLE->POINT) does not keep riding\n";

    // 3. A genuinely re-aimed run (master leg, portal leg, arrival) is a new
    // decision, not the same run: far from the in-flight destination.
    CHECK(!ShouldKeepRidingCorpseRun(false, true, POINT, POINT, 10.01f));
    CHECK(!ShouldKeepRidingCorpseRun(false, true, POINT, POINT, 150.0f));
    std::cout << "  [PASS] re-aimed run re-dispatches\n";

    // 4. Owned/hired bots and living bots never take the pool-bot path.
    CHECK(!ShouldKeepRidingCorpseRun(true, true, POINT, POINT, 0.0f));
    CHECK(!ShouldKeepRidingCorpseRun(false, false, POINT, POINT, 0.0f));
    std::cout << "  [PASS] owned/hired and living bots keep today's handling\n";

    // 5. Stall teleport fires on deadTime/distance only: armed 60-s anchor,
    // ground still to cover. No activity-gate input exists by design.
    CHECK(ShouldGhostStallTeleport(false, false, 1000, 1060));
    CHECK(ShouldGhostStallTeleport(false, false, 1000, 1120));
    std::cout << "  [PASS] armed 60-s stall teleports\n";

    // 6. Not yet stalled: the clock has not run out.
    CHECK(!ShouldGhostStallTeleport(false, false, 1000, 1059));
    std::cout << "  [PASS] fresh anchor does not teleport\n";

    // 7. Unarmed anchor (progress bookkeeping never ran) never fires.
    CHECK(!ShouldGhostStallTeleport(false, false, 0, 9999));
    std::cout << "  [PASS] unarmed anchor never fires\n";

    // 8. Inside core reclaim range the reclaim path fires without walking.
    CHECK(!ShouldGhostStallTeleport(false, true, 1000, 9999));
    std::cout << "  [PASS] reclaim-range ghost yields to reclaim\n";

    // 9. Owned/hired bots keep today's handling, however long the stall.
    CHECK(!ShouldGhostStallTeleport(true, false, 1000, 9999));
    std::cout << "  [PASS] owned/hired bots exempt from stall teleport\n";

    std::cout << "ghost-stall policy: OK\n";
    return 0;
}
