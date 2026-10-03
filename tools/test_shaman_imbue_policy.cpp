#include "../ai/playerbot/strategy/shaman/ShamanImbuePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BestKnownShamanImbue;
using ai::ShamanUpkeepShouldAttempt;

int main()
{
    std::cout << "Starting shaman imbue policy tests...\n";

    // No imbue known (fresh level 1 before the first trainer visit): no upkeep.
    CHECK(BestKnownShamanImbue(true, false, false, false, false) == "");
    CHECK(BestKnownShamanImbue(false, false, false, false, false) == "");
    std::cout << "  [PASS] no known imbue keeps upkeep quiet\n";

    // Low level: rockbiter is the only imbue gated at 1, so it must win.
    CHECK(BestKnownShamanImbue(true, false, false, false, true) == "rockbiter weapon");
    CHECK(BestKnownShamanImbue(false, false, false, false, true) == "rockbiter weapon");
    std::cout << "  [PASS] rockbiter is the low-level upkeep\n";

    // Level 10+: flametongue outranks rockbiter (class doc + donor chain).
    CHECK(BestKnownShamanImbue(true, false, true, false, true) == "flametongue weapon");
    CHECK(BestKnownShamanImbue(false, false, true, false, true) == "flametongue weapon");
    std::cout << "  [PASS] flametongue outranks rockbiter once trained\n";

    // Level 20+: flametongue still outranks frostbrand (rank 2/3 known by then).
    CHECK(BestKnownShamanImbue(true, false, true, true, true) == "flametongue weapon");
    CHECK(BestKnownShamanImbue(false, false, true, true, true) == "flametongue weapon");
    std::cout << "  [PASS] flametongue outranks frostbrand\n";

    // Frostbrand without flametongue still beats rockbiter.
    CHECK(BestKnownShamanImbue(true, false, false, true, true) == "frostbrand weapon");
    CHECK(BestKnownShamanImbue(false, false, false, true, true) == "frostbrand weapon");
    std::cout << "  [PASS] frostbrand is the fallback before windfury\n";

    // Level 30+: windfury wins outright for enhancement (best-in-slot).
    CHECK(BestKnownShamanImbue(true, true, true, true, true) == "windfury weapon");
    CHECK(BestKnownShamanImbue(true, true, false, false, false) == "windfury weapon");
    std::cout << "  [PASS] windfury wins for enhancement once trained\n";

    // Non-enhancement never takes windfury while flametongue is known.
    CHECK(BestKnownShamanImbue(false, true, true, true, true) == "flametongue weapon");
    std::cout << "  [PASS] non-enhancement keeps flametongue at 30+\n";

    // Any imbue beats none: windfury is the last resort, never an empty hand.
    CHECK(BestKnownShamanImbue(false, true, false, false, false) == "windfury weapon");
    std::cout << "  [PASS] windfury is the last resort rather than no imbue\n";

    // Upkeep tick gate (live ACTION_LOOP: sitting/casting/stunned bots failed
    // every tick on an already-imbued weapon). Only a known-but-missing
    // imbue on a castable bot may run.
    CHECK(ShamanUpkeepShouldAttempt(true, false, false) == true);
    CHECK(ShamanUpkeepShouldAttempt(false, false, false) == false);
    CHECK(ShamanUpkeepShouldAttempt(true, true, false) == false);
    CHECK(ShamanUpkeepShouldAttempt(true, false, true) == false);
    CHECK(ShamanUpkeepShouldAttempt(false, true, false) == false);
    std::cout << "  [PASS] upkeep stands down when nothing needs casting\n";

    std::cout << "All shaman imbue policy checks PASSED!\n";
    return 0;
}
