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
using ai::IsGolemaggSuppressedAoeAction;
using ai::ShouldBackOffSplash;
using ai::ShouldExcludeRager;

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

    // Back-off lockout is the >= 20 rule re-checked every tick (donor
    // MCMultipliers.cpp:136): the trigger fires the move at 20+, the veto
    // keeps non-tanks out while stacks stay >= 20, and expiry (stacks 0)
    // releases. Tanks and the burn phase are exempt either way.
    CHECK(ShouldBackOffSplash(false, 20, 50.0f));
    CHECK(!ShouldBackOffSplash(false, 0, 50.0f));
    CHECK(!ShouldBackOffSplash(true, 20, 50.0f));
    CHECK(!ShouldBackOffSplash(false, 20, 10.0f));
    std::cout << "  [PASS] back-off lockout rule\n";

    // AoE veto covers the frost mage pack spell: "blizzard" fires off the
    // same "ranged medium aoe" trigger as "flamestrike".
    CHECK(IsGolemaggSuppressedAoeAction("blizzard"));
    CHECK(IsGolemaggSuppressedAoeAction("flamestrike"));
    CHECK(!IsGolemaggSuppressedAoeAction("frostbolt"));
    std::cout << "  [PASS] aoe suppression names\n";

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
