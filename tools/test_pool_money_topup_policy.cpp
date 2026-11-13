#include "../runtime/PoolMoneyTopUpPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::PoolMoneyTopUpAmount;
using TortoiseBots::PoolMoneyTopUpDue;
using TortoiseBots::PoolMoneyTopUpEligible;
using TortoiseBots::PoolMoneyTopUpFloor;

int main()
{
    std::cout << "Starting TortoiseBots pool money top-up policy tests...\n";

    // Masterless pool bots only, behind the knob, never gold-cheat bots.
    CHECK(PoolMoneyTopUpEligible(true, true, false, false, false, false));
    CHECK(!PoolMoneyTopUpEligible(false, true, false, false, false, false));
    CHECK(!PoolMoneyTopUpEligible(true, false, false, false, false, false)); // alt / real player
    CHECK(!PoolMoneyTopUpEligible(true, true, true, false, false, false));   // hired companion
    CHECK(!PoolMoneyTopUpEligible(true, true, false, true, false, false));   // guild-claimed
    CHECK(!PoolMoneyTopUpEligible(true, true, false, false, true, false));   // grouped with a player
    CHECK(!PoolMoneyTopUpEligible(true, true, false, false, false, true));   // gold cheat
    std::cout << "  [PASS] only masterless pool bots, behind the knob\n";

    // Once per level.
    CHECK(PoolMoneyTopUpDue(14, 13));
    CHECK(PoolMoneyTopUpDue(14, 0));
    CHECK(!PoolMoneyTopUpDue(14, 14));
    std::cout << "  [PASS] once per level\n";

    // Floor = trainer-visit need, capped at 1 g per level.
    CHECK(PoolMoneyTopUpFloor(1700, 13) == 1700);         // 17 s rank at 13
    CHECK(PoolMoneyTopUpFloor(500000, 13) == 130000);     // 50 g backlog -> 13 g cap
    CHECK(PoolMoneyTopUpFloor(0, 30) == 0);               // nothing to train
    CHECK(PoolMoneyTopUpFloor(0xFFFFFFFFu, 60) == 600000);
    std::cout << "  [PASS] floor is the trainer need, capped per level\n";

    // Amount: up to the floor only.
    CHECK(PoolMoneyTopUpAmount(1038, 1700) == 662);  // Gamorak: 10 s 38 c vs a 17 s need
    CHECK(PoolMoneyTopUpAmount(1700, 1700) == 0);
    CHECK(PoolMoneyTopUpAmount(50000, 1700) == 0);   // not broke: nothing
    CHECK(PoolMoneyTopUpAmount(0, 0) == 0);
    std::cout << "  [PASS] tops up to the floor, never above\n";

    std::cout << "All pool money top-up policy checks PASSED!\n";
    return 0;
}
