// Standalone regression test for the healer mana-conservation port (night2
// gaps 1+2): never veto a target in danger, veto oversized/inefficient heals
// on comfortable targets, veto mana-hungry heals while the healer is low,
// and keep the balance-scaled mana reserve before healer off-spec damage.
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

    // Healer off-spec damage reserve (`healer should attack`), by balance.
    // Hard fight (balance <= 50) keeps the donor's 85; even (<= 100) and
    // easy (> 100) fights keep 70, not the donor's 65 / 40.
    CHECK(HealerDpsManaFloor(0) == 85);
    CHECK(HealerDpsManaFloor(50) == 85);
    CHECK(HealerDpsManaFloor(51) == 70);
    CHECK(HealerDpsManaFloor(100) == 70);
    CHECK(HealerDpsManaFloor(101) == 70);
    CHECK(HealerDpsManaFloor(200) == 70);
    // Monotonic: a harder fight (lower balance) never allows a lower reserve.
    for (int b = 1; b <= 200; ++b)
        CHECK(HealerDpsManaFloor(std::uint8_t(b - 1)) >= HealerDpsManaFloor(std::uint8_t(b)));
    // Uldaman case: full 5-man (5 x 45) vs two elites (2 x 37 x 3) gives
    // balance 101; the priest stops nuking at 70 %, no longer at 40 %.
    CHECK(HealerDpsManaAllows(true, 70, 101));
    CHECK(!HealerDpsManaAllows(true, 69, 101));
    CHECK(!HealerDpsManaAllows(true, 40, 200));
    CHECK(HealerDpsManaAllows(true, 100, 200));
    // Even fight: 65 % no longer enough.
    CHECK(HealerDpsManaAllows(true, 70, 100));
    CHECK(!HealerDpsManaAllows(true, 65, 100));
    // Hard fight unchanged.
    CHECK(HealerDpsManaAllows(true, 85, 50));
    CHECK(!HealerDpsManaAllows(true, 84, 50));
    // No mana bar: never held back by the reserve.
    CHECK(HealerDpsManaAllows(false, 0, 101));
    CHECK(HealerDpsManaAllows(false, 0, 10));

    // Healer damage target: solo or no target -> as before; grouped, only a
    // mob that already fights another party member (Uldaman 2026-10-14:
    // the priest opened on a mob the tank had not touched and took it).
    CHECK(HealerDamageTargetAllowed(false, true, false));
    CHECK(HealerDamageTargetAllowed(true, false, false));
    CHECK(HealerDamageTargetAllowed(true, true, true));
    CHECK(!HealerDamageTargetAllowed(true, true, false));

    std::printf("healer mana policy: OK (%d checks)\n", checks);
    return 0;
}
