#include "../ai/playerbot/TravelRepickPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ShouldKeepTravelAcrossStuckReset;
using ai::ShouldRetireStuckTravelKeep;
using ai::TravelInvalidParkKey;
using ai::TravelIsResetToNull;
using ai::TravelMoveFailPathTag;
using ai::TravelPurposeParkKey;
using ai::TravelTargetIsNull;

int main()
{
    std::cout << "Starting TortoiseBots travel repick policy tests...\n";

    // (a) Keep rule: an active target with a real destination and position
    // survives the stuck reset, mirroring the move-stuck path. Anything else
    // resets bare.
    CHECK(ShouldKeepTravelAcrossStuckReset(true, true, true, true, false));
    CHECK(!ShouldKeepTravelAcrossStuckReset(false, false, false, false, false));
    CHECK(!ShouldKeepTravelAcrossStuckReset(true, false, true, true, false));
    CHECK(!ShouldKeepTravelAcrossStuckReset(true, true, false, true, false));
    CHECK(!ShouldKeepTravelAcrossStuckReset(true, true, true, false, false));
    CHECK(!ShouldKeepTravelAcrossStuckReset(true, true, true, true, true));
    std::cout << "  [PASS] stuck keep rule keeps only active real targets\n";

    // Bounded keep: 3 consecutive keeps without progress (inside 30 yd of
    // the keep anchor) retires the target instead of preserving it.
    CHECK(!ShouldRetireStuckTravelKeep(0, 0.0f));
    CHECK(!ShouldRetireStuckTravelKeep(2, 0.0f));
    CHECK(ShouldRetireStuckTravelKeep(3, 29.0f * 29.0f));
    CHECK(!ShouldRetireStuckTravelKeep(3, 30.0f * 30.0f));
    CHECK(!ShouldRetireStuckTravelKeep(3, 100.0f * 100.0f));
    CHECK(ShouldRetireStuckTravelKeep(4, 0.0f));
    std::cout << "  [PASS] stuck keep retirement needs 3 keeps without progress\n";

    // (c) The invalid-result park files under the same key the empty-search
    // path and the request gate use, and lasts one minute.
    CHECK(TravelInvalidParkKey("") == "quest");
    CHECK(TravelInvalidParkKey("quest") == "quest");
    CHECK(TravelInvalidParkKey("4096") == "4096");
    CHECK(ai::TRAVEL_FUTURE_INVALID_PARK_SECONDS == 60);
    std::cout << "  [PASS] invalid-result park key and duration\n";

    // (d) Null recognition: no destination, or the null destination. A real
    // destination is not null. The caller snapshots the old target BEFORE
    // CopyTarget and reads the new target for the reset skip.
    CHECK(TravelTargetIsNull(false, false));
    CHECK(TravelTargetIsNull(true, true));
    CHECK(!TravelTargetIsNull(true, false));
    std::cout << "  [PASS] null-target recognition\n";

    // Reset skip: a fresh null pick (new side null) stays quiet whatever the
    // stale purpose says, preserving the earlier old-null/None skip; a
    // from-null pick of a real purpose logs with the marker.
    CHECK(TravelIsResetToNull(false, true, "quest"));
    CHECK(TravelIsResetToNull(true, true, "None"));
    CHECK(TravelIsResetToNull(true, false, "None"));
    CHECK(!TravelIsResetToNull(true, false, "quest"));
    CHECK(!TravelIsResetToNull(false, false, "None"));
    CHECK(!TravelIsResetToNull(false, false, "quest"));
    std::cout << "  [PASS] reset-to-null skip keeps normal picks logging\n";

    // (e) Purpose-park key: the drop/retire path parks exactly the key the
    // request gate reads back ("quest" for the quest errand, which carries no
    // qualifier). An empty purpose - wiped by a reset - parks the quest
    // errand instead of nothing (finding 1 + §1.2: 514 empty fails, 1,658
    // empty drops parked key "" while requests checked "quest").
    CHECK(TravelPurposeParkKey("") == "quest");
    CHECK(TravelPurposeParkKey("quest") == "quest");
    CHECK(TravelPurposeParkKey("4096") == "4096");
    CHECK(TravelPurposeParkKey("64") == "64");
    CHECK(TravelPurposeParkKey("512") == "512");
    CHECK(TravelPurposeParkKey("trainer class") == "trainer class");
    std::cout << "  [PASS] purpose-park key matches the request gate\n";

    // (f) Move-fail path tag: NOPATH vs INCOMPLETE vs unloaded tile vs full
    // path, from the raw PathFinder::getPathType() bits (finding 14).
    CHECK(TravelMoveFailPathTag(0x0008) == "nopath");
    CHECK(TravelMoveFailPathTag(0x0004) == "incomplete");
    CHECK(TravelMoveFailPathTag(0x0010) == "not-using-path");
    CHECK(TravelMoveFailPathTag(0x0001) == "complete");
    CHECK(TravelMoveFailPathTag(0x0000) == "complete");
    // NOT_USING_PATH wins when combined: an unloaded tile says nothing about
    // reachability, like the taker probe treats it.
    CHECK(TravelMoveFailPathTag(0x0008 | 0x0010) == "not-using-path");
    std::cout << "  [PASS] move-fail path tag buckets the pathfinder result\n";

    std::cout << "travel repick policy: OK\n";
    return 0;
}
