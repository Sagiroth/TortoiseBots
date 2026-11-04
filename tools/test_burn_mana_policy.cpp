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

// A member with nothing against it: alive, in combat, in control, off cooldown, enough mana and in range.
static bool Available(bool lostControl = false, bool silencedOrPacified = false, bool charmed = false,
                      bool onCooldown = false, int mana = 500, int manaCost = 300, float distanceYd = 10.0f,
                      float minRangeYd = 0.0f, float maxRangeYd = 20.0f)
{
    return BurnManaAvailableNow(true, true, lostControl, silencedOrPacified, charmed, onCooldown, mana, manaCost,
                                distanceYd, minRangeYd, maxRangeYd);
}

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
    CHECK(Available(false, false, false, false, 500, 300, 19.0f, 0.0f, 20.0f)); // WarlockInRange
    CHECK(!Available(false, false, false, false, 500, 300, 26.0f, 0.0f, 20.0f)); // WarlockOutOfRange
    CHECK(Available(false, false, false, false, 500, 300, 25.0f, 0.0f, 30.0f)); // PriestInRange
    CHECK(Available(false, false, false, false, 500, 300, 30.0f, 0.0f, 30.0f)); // RangeThresholdInclusive
    CHECK(!Available(false, false, false, false, 500, 300, 36.0f, 8.0f, 35.0f)); // HunterTooFar
    CHECK(!Available(false, false, false, false, 500, 300, 5.0f, 8.0f, 35.0f)); // HunterInsideMinRange
    CHECK(!BurnManaAvailableNow(true, false, false, false, false, false, 500, 300, 20.0f, 0.0f, 20.0f)); // OutOfCombat
    CHECK(!BurnManaAvailableNow(false, true, false, false, false, false, 500, 300, 20.0f, 0.0f, 20.0f)); // Dead
    CHECK(!Available(false, false, false, false, 299, 300)); // NoManaForCast
    CHECK(Available(false, false, false, false, 300, 300)); // ExactMana

    // A member held out of control holds no slot: stunned, feared, confused, silenced, pacified or charmed.
    CHECK(!Available(true)); // StunnedOrFearedOrConfused
    CHECK(!Available(false, true)); // SilencedOrPacified
    CHECK(!Available(false, false, true)); // Charmed
    // A burn spell on cooldown holds no slot either.
    CHECK(!Available(false, false, false, true)); // SpellOnCooldown
    // Merely being on the global cooldown, mid-cast or moving is not an input: those are transient, and changing the
    // rank for one GCD would rotate the burners. The caller reports only the spell's own cooldown, so a bot on the GCD
    // stays available.
    CHECK(Available()); // GcdOrCastingKeepsTheSlot

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
        {10, true, Available(false, false, false, false, 800, 300, 26.0f, 0.0f, 20.0f)},
        {20, true, Available(false, false, false, false, 800, 300, 26.0f, 0.0f, 20.0f)},
        {30, true, Available(false, false, false, false, 800, 300, 26.0f, 0.0f, 20.0f)},
        {40, true, Available(false, false, false, false, 800, 300, 26.0f, 0.0f, 20.0f)},
        {50, true, Available(false, false, false, false, 800, 300, 25.0f, 0.0f, 30.0f)},
    };
    for (int i = 0; i < 4; ++i)
        CHECK(!aq20[i].canBurnNow);
    CHECK(aq20[4].canBurnNow);
    int const priestRank = BurnManaRank(aq20, 5, 50, true, true);
    CHECK(priestRank == 0); // OutOfRangeWarlocksHoldNoSlot
    CHECK(BurnManaWanted(50.0f, 10.0f, true, priestRank, 4, false)); // PriestBurnsUnderTheCap

    // The lower-guid burner is stunned, silenced or charmed, or its spell is on cooldown: it holds no slot, so the
    // higher-guid castable bot ranks 0 and burns.
    BurnManaCandidate controlled[] = {
        {10, true, Available(true)},
        {20, true, Available(false, true)},
        {30, true, Available(false, false, true)},
        {40, true, Available(false, false, false, true)},
        {50, true, Available()},
    };
    for (int i = 0; i < 4; ++i)
        CHECK(!controlled[i].canBurnNow);
    CHECK(controlled[4].canBurnNow);
    int const freeRank = BurnManaRank(controlled, 5, 50, true, true);
    CHECK(freeRank == 0); // HeldOutOfControlHoldsNoSlot
    CHECK(BurnManaWanted(50.0f, 10.0f, true, freeRank, 4, false)); // FreeBotBurnsUnderTheCap

    // A lower-guid bot merely on the GCD keeps its slot: it stays available, so the higher-guid bot keeps rank 1.
    BurnManaCandidate busyOneGcd[] = {
        {10, true, Available(false, false, false, false,
                             500, 300, 10.0f, 0.0f, 20.0f)},
        {20, true, Available()},
    };
    CHECK(BurnManaRank(busyOneGcd, 2, 20, true, true) == 1); // GcdDoesNotRotateTheRank
    CHECK(!BurnManaWanted(50.0f, 10.0f, true, 4, 4, false));

    std::cout << "All burn mana policy tests passed.\n";
    return 0;
}
