// Standalone regression test for the healer mana-conservation port (night2
// gaps 1+2): never veto a target in danger, veto oversized/inefficient heals
// on comfortable targets, veto mana-hungry heals while the healer is low.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_healer_mana_policy.cpp -o /tmp/test_healer_mana
//   /tmp/test_healer_mana

#include "../ai/playerbot/HealerManaPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace ai;
using ai::HealManaEfficiency;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static HealerManaState Base()
{
    // Mid-health non-tank, comfortable healer mana, medium spell.
    return HealerManaState{60, 80, 15, HealManaEfficiency::MEDIUM, false, 50, 70, 40};
}

int main()
{
    // Danger line never vetoes, even for a huge inefficient heal on fumes.
    CHECK(ShouldStartHeal(HealerManaState{50, 0, 100, HealManaEfficiency::LOW, false, 50, 70, 40}));
    CHECK(ShouldStartHeal(HealerManaState{20, 0, 100, HealManaEfficiency::LOW, true, 50, 70, 40}));
    CHECK(ShouldStartHeal(HealerManaState{0, 0, 100, HealManaEfficiency::LOW, false, 50, 70, 40}));

    // Comfortable target (>= medium): oversized heal vetoed...
    CHECK(!ShouldStartHeal(HealerManaState{85, 90, 50, HealManaEfficiency::MEDIUM, false, 50, 70, 40}));
    // ...even when efficient (loss < est still wastes most of the cast).
    CHECK(!ShouldStartHeal(HealerManaState{85, 90, 50, HealManaEfficiency::VERY_HIGH, false, 50, 70, 40}));
    // ...and a merely MEDIUM spell is vetoed on a comfortable target even
    // when it fits (donor prefers HoTs/shields up here).
    CHECK(!ShouldStartHeal(HealerManaState{80, 90, 15, HealManaEfficiency::MEDIUM, false, 50, 70, 40}));
    // A cheap efficient heal that fits still fires on a comfortable target.
    CHECK(ShouldStartHeal(HealerManaState{80, 90, 15, HealManaEfficiency::HIGH, false, 50, 70, 40}));
    CHECK(ShouldStartHeal(HealerManaState{80, 90, 15, HealManaEfficiency::VERY_HIGH, false, 50, 70, 40}));

    // Between low and medium: efficient fitting heals fire...
    CHECK(ShouldStartHeal(Base()));
    CHECK(ShouldStartHeal(HealerManaState{60, 80, 15, HealManaEfficiency::HIGH, false, 50, 70, 40}));
    // ...but a LOW spell is vetoed while the healer is below reserve...
    CHECK(!ShouldStartHeal(HealerManaState{60, 30, 15, HealManaEfficiency::LOW, false, 50, 70, 40}));
    // ...and fires again once the healer recovers.
    CHECK(ShouldStartHeal(HealerManaState{60, 40, 15, HealManaEfficiency::LOW, false, 50, 70, 40}));
    // LOW spell on a comfortable target is vetoed regardless of mana.
    CHECK(!ShouldStartHeal(HealerManaState{80, 90, 15, HealManaEfficiency::LOW, false, 50, 70, 40}));

    // Tanks count the estimate at 2/3 (bigger bars): a 30-est heal that is
    // oversized for a 75% non-tank (loss 25 < 30) still fires on a 75% tank
    // (effective est 20 < 25).
    CHECK(!ShouldStartHeal(HealerManaState{75, 90, 30, HealManaEfficiency::HIGH, false, 50, 70, 40}));
    CHECK(ShouldStartHeal(HealerManaState{75, 90, 30, HealManaEfficiency::HIGH, true, 50, 70, 40}));

    // Full-health target: loss 0, any estimate overshoots.
    CHECK(!ShouldStartHeal(HealerManaState{100, 90, 15, HealManaEfficiency::VERY_HIGH, false, 50, 70, 40}));

    std::printf("healer mana policy: OK (%d checks)\n", checks);
    return 0;
}
