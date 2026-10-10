// Standalone regression test for the mage arcane rupture -> missiles
// rhythm port (MAG-5): rupture while the self buff is absent, missiles
// (the arcane default) while it is up.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_arcane_rupture_policy.cpp -o /tmp/test_arcane_rupture
//   /tmp/test_arcane_rupture

#include "../ai/playerbot/ArcaneRupturePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    // Cast-ID guard: spellbook ranks 51949-51954 count; the 51955-51960
    // trainer learn-spell wrappers, buffs, and neighbours do not.
    for (std::uint32_t id = 51949; id <= 51954; ++id)
        CHECK(IsArcaneRuptureCastId(id));
    CHECK(!IsArcaneRuptureCastId(51955));
    CHECK(!IsArcaneRuptureCastId(51960));
    CHECK(!IsArcaneRuptureCastId(52502));
    CHECK(!IsArcaneRuptureCastId(52588));
    CHECK(!IsArcaneRuptureCastId(51948));
    CHECK(!IsArcaneRuptureCastId(51961));
    CHECK(!IsArcaneRuptureCastId(0));

    // Buff absent + known: rupture now.
    CHECK(ShouldCastArcaneRupture(ArcaneRuptureState{false, true, false}));

    // Buff up: missiles own the GCD, no rupture row.
    CHECK(!ShouldCastArcaneRupture(ArcaneRuptureState{true, true, false}));

    // Untrained (low level): quiet — the HasSpell gate refuses.
    CHECK(!ShouldCastArcaneRupture(ArcaneRuptureState{false, false, false}));
    CHECK(!ShouldCastArcaneRupture(ArcaneRuptureState{true, false, false}));

    std::printf("arcane rupture policy: %d checks passed\n", checks);
    return 0;
}
