#include "../ai/playerbot/GolemaggPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsSingleLivingTank;
using ai::kCoreRagerEntry;
using ai::kGolemaggBurnPct;
using ai::kGolemaggEntry;
using ai::kGolemaggHealerTolerance;
using ai::kGolemaggTrustDistance;
using ai::kMagmaSplashBackOffDistance;
using ai::kMagmaSplashBackOffStacks;
using ai::kMagmaSplashSpellId;
using ai::ShouldBackOffSplash;
using ai::ShouldExcludeRager;
using ai::ShouldHoldBackOff;

int main()
{
    std::cout << "Starting TortoiseBots golemagg policy tests...\n";

    // Pinned ids and constants.
    CHECK(kGolemaggEntry == 11988);
    CHECK(kCoreRagerEntry == 11672);
    CHECK(kMagmaSplashSpellId == 13880);
    CHECK(kMagmaSplashBackOffStacks == 20);
    CHECK(kMagmaSplashBackOffDistance == 12.0f);
    CHECK(kGolemaggHealerTolerance == 8.0f);
    CHECK(kGolemaggTrustDistance == 30.0f);
    CHECK(kGolemaggBurnPct == 10.0f);
    std::cout << "  [PASS] ids and constants pinned\n";

    // Splash back-off: non-tanks at 20+ stacks leave; tanks never do, and
    // the burn phase releases everyone.
    CHECK(ShouldBackOffSplash(false, 20, 50.0f));
    CHECK(ShouldBackOffSplash(false, 40, 50.0f));
    CHECK(!ShouldBackOffSplash(false, 19, 50.0f));
    CHECK(!ShouldBackOffSplash(true, 40, 50.0f));
    CHECK(!ShouldBackOffSplash(false, 40, 10.0f));
    CHECK(!ShouldBackOffSplash(false, 40, 5.0f));
    std::cout << "  [PASS] splash back-off rule\n";

    // Back-off lock: any remaining stack holds non-tanks out until it
    // expires; tanks and burn phase exempt.
    CHECK(ShouldHoldBackOff(false, true, 50.0f));
    CHECK(!ShouldHoldBackOff(false, false, 50.0f));
    CHECK(!ShouldHoldBackOff(true, true, 50.0f));
    CHECK(!ShouldHoldBackOff(false, true, 10.0f));
    std::cout << "  [PASS] back-off lock rule\n";

    // Rager exclusion while Golemagg lives; single-tank shortcut.
    CHECK(ShouldExcludeRager(true));
    CHECK(!ShouldExcludeRager(false));
    CHECK(IsSingleLivingTank(1));
    CHECK(!IsSingleLivingTank(2));
    CHECK(!IsSingleLivingTank(0));
    std::cout << "  [PASS] rager exclusion and single-tank rules\n";

    std::cout << "All golemagg policy tests passed.\n";
    return 0;
}
