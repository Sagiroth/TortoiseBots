#include "../ai/playerbot/HunterSwitchPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kHunterMeleeGlueYd;
using ai::kHunterRangedReturnYd;
using ai::ShouldSwitchToMelee;
using ai::ShouldSwitchToRanged;

int main()
{
    std::cout << "Starting TortoiseBots hunter-switch hysteresis tests...\n";

    // Band edges: melee at 5 and below, ranged back at 10 and above.
    CHECK(kHunterMeleeGlueYd == 5.0f);
    CHECK(kHunterRangedReturnYd == 10.0f);
    CHECK(kHunterMeleeGlueYd < kHunterRangedReturnYd);

    // Melee side: glued fast victim trades, everything else holds.
    {
        // Glued fast victim at the edge: trade.
        CHECK(ShouldSwitchToMelee(true, true, false, true, 5.0f, true));
        // Just outside the glue edge: hold ranged even glued-ish.
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 5.1f, true));
        // Mid-band holds the kit: neither switch fires here (ranged half
        // needs 10+, see below).
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 7.0f, true));
        CHECK(!ShouldSwitchToRanged(true, false, false, false, 7.0f, true));
        // Victim off the bot, immobilized, or slow non-finisher: hold.
        CHECK(!ShouldSwitchToMelee(true, false, false, true, 3.0f, true));
        CHECK(!ShouldSwitchToMelee(true, true, true, true, 3.0f, true));
        CHECK(!ShouldSwitchToMelee(true, true, false, false, 3.0f, true));
        // Slow finisher (pet gone, mob nearly dead) still trades when glued.
        CHECK(ShouldSwitchToMelee(true, true, false, true, 4.0f, true));
        // No ranged kit to trade from: hold.
        CHECK(!ShouldSwitchToMelee(false, true, false, true, 3.0f, true));
        // Out of ammo always trades: nothing to shoot with.
        CHECK(ShouldSwitchToMelee(true, false, true, false, 30.0f, false));
        std::cout << "  [PASS] melee trades only when glued, victim on bot, mobile\n";
    }

    // Ranged side: off-bot / immobilized / slow / 10+ yd restores ranged.
    {
        // Made distance: back to ranged at the edge.
        CHECK(ShouldSwitchToRanged(true, false, false, false, 10.0f, true));
        // Just inside the edge with nothing else: hold melee.
        CHECK(!ShouldSwitchToRanged(true, false, false, false, 9.9f, true));
        // Off-bot, immobilized, or too slow: ranged at any distance.
        CHECK(ShouldSwitchToRanged(true, true, false, false, 3.0f, true));
        CHECK(ShouldSwitchToRanged(true, false, true, false, 3.0f, true));
        CHECK(ShouldSwitchToRanged(true, false, false, true, 3.0f, true));
        // No close kit to trade back from, or no ammo: hold.
        CHECK(!ShouldSwitchToRanged(false, true, false, false, 30.0f, true));
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 30.0f, false));
        std::cout << "  [PASS] ranged returns off-bot, immobilized, slow, or 10+ yd\n";
    }

    std::cout << "All hunter-switch hysteresis checks PASSED!\n";
    return 0;
}
