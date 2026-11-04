// Standalone check for ai/playerbot/policy/raid/BurnManaPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_burn_mana_policy.cpp -o /tmp/test_burn_mana_policy && /tmp/test_burn_mana_policy
#include "../ai/playerbot/policy/raid/BurnManaPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    // Moam's mana is full while drain mana keeps refilling it: a burner keeps burning above the row's start %.
    CHECK(BurnManaWanted(80.0f, 10.0f, true, 0, 4, false)); // BurnsWhileAboveStart
    // Below the start % the boss is filling up on his own drain casts and the burners go back to the raid.
    CHECK(!BurnManaWanted(9.0f, 10.0f, true, 0, 4, false)); // IdleBelowStart
    // `priority` is the most burners; the ranks are 0-based, so rank 4 is the fifth bot and sits out a cap of 4.
    CHECK(!BurnManaWanted(80.0f, 10.0f, true, 4, 4, false)); // CapsAtMaxBurners
    CHECK(BurnManaWanted(80.0f, 10.0f, true, 3, 4, false));
    // A cap of 0 means every eligible bot burns.
    CHECK(BurnManaWanted(80.0f, 10.0f, true, 9, 0, false)); // ZeroCapMeansAll
    // A bot with none of the three spells never burns, whatever the mana.
    CHECK(!BurnManaWanted(80.0f, 10.0f, false, 0, 0, false));
    // During the stone phase the boss's mana does not threaten the raid: even the first burner sits out.
    CHECK(!BurnManaWanted(80.0f, 10.0f, true, 0, 4, true)); // StonePhaseSkips

    // Range and cost are the member's own spell's: warlock Drain Mana 20 yd, priest Mana Burn 30 yd, hunter Viper
    // Sting 8-35 yd.
    CHECK(BurnManaInRangeNow(true, true, 500, 300, 19.0f, 0.0f, 20.0f)); // WarlockInRange
    CHECK(!BurnManaInRangeNow(true, true, 500, 300, 26.0f, 0.0f, 20.0f)); // WarlockOutOfRange
    CHECK(BurnManaInRangeNow(true, true, 500, 300, 25.0f, 0.0f, 30.0f)); // PriestInRange
    CHECK(BurnManaInRangeNow(true, true, 500, 300, 30.0f, 0.0f, 30.0f)); // RangeThresholdInclusive
    CHECK(!BurnManaInRangeNow(true, true, 500, 300, 36.0f, 8.0f, 35.0f)); // HunterTooFar
    CHECK(!BurnManaInRangeNow(true, true, 500, 300, 5.0f, 8.0f, 35.0f)); // HunterInsideMinRange
    CHECK(!BurnManaInRangeNow(true, false, 500, 300, 20.0f, 0.0f, 20.0f)); // OutOfCombat
    CHECK(!BurnManaInRangeNow(false, true, 500, 300, 20.0f, 0.0f, 20.0f)); // Dead
    CHECK(!BurnManaInRangeNow(true, true, 299, 300, 20.0f, 0.0f, 20.0f)); // NoManaForCast
    CHECK(BurnManaInRangeNow(true, true, 300, 300, 20.0f, 0.0f, 20.0f)); // ExactMana

    // A lower-guid member that runs no boss tactics never burns, so it holds no capped slot.
    BurnManaCandidate noTactics[] = {
        {1, false, true},
        {2, true, true},
    };
    CHECK(BurnManaRank(noTactics, 2, 3, true, true) == 1); // RankSkipsWithoutBossTactics
    // A lower-guid member that cannot cast right now holds no slot either.
    BurnManaCandidate outOfRange[] = {
        {1, true, false},
        {2, true, true},
    };
    CHECK(BurnManaRank(outOfRange, 2, 3, true, true) == 1); // RankSkipsCannotCastNow
    // The bot itself must run the tactics and be able to cast now, or it has no rank at all.
    CHECK(BurnManaRank(noTactics, 2, 3, false, true) == -1); // SelfWithoutBossTactics
    CHECK(BurnManaRank(noTactics, 2, 3, true, false) == -1); // SelfOutOfRange

    // The four lowest guids are warlocks 26 yd away (Drain Mana reaches 20 yd), the fifth is a priest 25 yd away
    // (Mana Burn reaches 30 yd): the warlocks hold no slot, so the priest ranks 0 and burns under a cap of 4.
    BurnManaCandidate aq20[] = {
        {10, true, BurnManaInRangeNow(true, true, 800, 300, 26.0f, 0.0f, 20.0f)},
        {20, true, BurnManaInRangeNow(true, true, 800, 300, 26.0f, 0.0f, 20.0f)},
        {30, true, BurnManaInRangeNow(true, true, 800, 300, 26.0f, 0.0f, 20.0f)},
        {40, true, BurnManaInRangeNow(true, true, 800, 300, 26.0f, 0.0f, 20.0f)},
        {50, true, BurnManaInRangeNow(true, true, 800, 300, 25.0f, 0.0f, 30.0f)},
    };
    for (int i = 0; i < 4; ++i)
        CHECK(!aq20[i].canBurnNow);
    CHECK(aq20[4].canBurnNow);
    int const priestRank = BurnManaRank(aq20, 5, 50, true, true);
    CHECK(priestRank == 0); // OutOfRangeWarlocksHoldNoSlot
    CHECK(BurnManaWanted(50.0f, 10.0f, true, priestRank, 4, false)); // PriestBurnsUnderTheCap

    std::cout << "All burn mana policy tests passed.\n";
    return 0;
}
