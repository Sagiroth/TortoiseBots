#include "../runtime/PoolGearTopUpPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::PoolGearTopUpDue;
using TortoiseBots::PoolGearTopUpEligible;
using TortoiseBots::PoolGearTopUpSlot;

int main()
{
    std::cout << "Starting TortoiseBots pool gear top-up policy tests...\n";

    // Masterless pool bots only; the knob closes it for everyone.
    CHECK(PoolGearTopUpEligible(true, true, false, false, false));
    CHECK(!PoolGearTopUpEligible(false, true, false, false, false));
    CHECK(!PoolGearTopUpEligible(true, false, false, false, false)); // alt / real player
    CHECK(!PoolGearTopUpEligible(true, true, true, false, false));   // hired companion
    CHECK(!PoolGearTopUpEligible(true, true, false, true, false));   // guild-claimed
    CHECK(!PoolGearTopUpEligible(true, true, false, false, true));   // grouped with a player
    std::cout << "  [PASS] only masterless pool bots, behind the knob\n";

    // Once per level, never below 5 (starting outfit stays).
    CHECK(PoolGearTopUpDue(12, 11));
    CHECK(PoolGearTopUpDue(12, 0));   // first check after a restart
    CHECK(!PoolGearTopUpDue(12, 12)); // already handled this level
    CHECK(!PoolGearTopUpDue(4, 0));
    CHECK(PoolGearTopUpDue(5, 0));
    std::cout << "  [PASS] once per level from level 5\n";

    // Empty slots are always filled.
    CHECK(PoolGearTopUpSlot(false, 0, 0, 15));
    // Grey/white more than 5 item levels below the bot: replaced.
    CHECK(PoolGearTopUpSlot(true, 1, 1, 7));   // level-1 starter white at 7
    CHECK(!PoolGearTopUpSlot(true, 1, 1, 6));  // ... but not yet at 6
    CHECK(PoolGearTopUpSlot(true, 0, 9, 15));  // grey ilvl 9 at 15
    CHECK(!PoolGearTopUpSlot(true, 1, 12, 15)); // recent white kept
    // Green or better is never touched, however old.
    CHECK(!PoolGearTopUpSlot(true, 2, 5, 20));
    CHECK(!PoolGearTopUpSlot(true, 3, 10, 40));
    CHECK(!PoolGearTopUpSlot(true, 4, 1, 60));
    std::cout << "  [PASS] empty or outdated grey/white slots only, green+ untouched\n";

    std::cout << "All pool gear top-up policy checks PASSED!\n";
    return 0;
}
