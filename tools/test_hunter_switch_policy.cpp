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
    std::cout << "Starting TortoiseBots hunter-switch tests...\n";

    // Edges: the donor's own 8 yd both ways (melee <= 8, ranged > 8); the
    // 10 yd constant is the return margin kept for future use.
    CHECK(kHunterMeleeGlueYd == 8.0f);
    CHECK(kHunterRangedReturnYd == 10.0f);

    // Melee side: the donor AND (victim on bot, dist <= 8) plus mobile.
    {
        // Glued victim at the edge: trade (fast or slow - the speed OR is
        // gone, a slow mob glued on the bot is still a dead-zone mob).
        CHECK(ShouldSwitchToMelee(true, true, false, true, 8.0f, true));
        CHECK(ShouldSwitchToMelee(true, true, false, false, 8.0f, true));
        // Just outside the edge: hold ranged even with the victim on bot.
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 8.1f, true));
        // Victim off the bot or rooted: hold (rooted is shot, not charged).
        CHECK(!ShouldSwitchToMelee(true, false, false, true, 3.0f, true));
        CHECK(!ShouldSwitchToMelee(true, true, true, true, 3.0f, true));
        // No ranged kit to trade from: hold.
        CHECK(!ShouldSwitchToMelee(false, true, false, true, 3.0f, true));
        // Out of ammo always trades: nothing to shoot with.
        CHECK(ShouldSwitchToMelee(true, false, true, false, 30.0f, false));
        std::cout << "  [PASS] melee trades only when the victim is glued on the bot\n";
    }

    // Ranged side: the donor AND (off bot AND past the dead zone) or rooted.
    {
        // Off the bot past the edge: back to ranged.
        CHECK(ShouldSwitchToRanged(true, true, false, false, 8.1f, true));
        // At the edge with the victim off: hold melee (donor uses > 8).
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 8.0f, true));
        // Victim still on the bot: hold even far out (melee finishes it).
        CHECK(!ShouldSwitchToRanged(true, false, false, false, 30.0f, true));
        // Rooted: ranged at any distance, even glued on the bot.
        CHECK(ShouldSwitchToRanged(true, false, true, false, 3.0f, true));
        // Merely slow no longer answers at melee distance (the flap source:
        // 83% of melee->ranged pairs landed at dist < 5 yd on this branch).
        CHECK(!ShouldSwitchToRanged(true, false, false, true, 3.0f, true));
        // No close kit to trade back from, or no ammo: hold.
        CHECK(!ShouldSwitchToRanged(false, true, false, false, 30.0f, true));
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 30.0f, false));
        std::cout << "  [PASS] ranged returns off-bot past 8 yd, or rooted\n";
    }

    std::cout << "All hunter-switch checks PASSED!\n";
    return 0;
}
