// Standalone check for ai/playerbot/policy/raid/TankSwapPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_tank_swap_policy.cpp -o /tmp/test_tank_swap_policy && /tmp/test_tank_swap_policy
#include "../ai/playerbot/policy/raid/TankSwapPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    // FIRST: the live Kurinnaxx case. The main tank (rotation 0) carries the row's 5 Mortal Wound stacks and off-tank
    // 1 (rotation 1) is clean: off-tank 1 takes the boss.
    {
        const std::vector<int> stacks{5, 0};
        const std::vector<bool> alive{true, true};
        const std::vector<bool> inRange{true, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, inRange) == 1);
    }

    // Below the row's threshold nobody swaps, however clean the other tank is.
    {
        const std::vector<int> stacks{4, 0};
        const std::vector<bool> alive{true, true};
        const std::vector<bool> inRange{true, true};
        CHECK(TankSwapWanted(0, 4, 5, stacks, alive, inRange) == -1);
        CHECK(TankSwapWanted(0, 0, 5, stacks, alive, inRange) == -1);
    }

    // A dead or out-of-reach tank is skipped, and the search still finds the next ready one past it.
    {
        const std::vector<int> stacks{5, 0, 0};
        const std::vector<bool> dead{true, false, true};
        const std::vector<bool> alive{true, true, true};
        const std::vector<bool> inRange{true, true, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, dead, inRange) == 2);
        const std::vector<bool> far{true, false, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, far) == 2);
        // Nobody ready: no swap rather than a taunt from a corpse or from across the room.
        const std::vector<bool> allDead{true, false, false};
        CHECK(TankSwapWanted(0, 5, 5, stacks, allDead, inRange) == -1);
    }

    // A candidate that still carries a deep stack is not a swap: at 3 of a threshold of 5 it would only add stacks
    // faster (fewer than half the threshold, 2.5, is the bar).
    {
        const std::vector<int> stacks{5, 3};
        const std::vector<bool> alive{true, true};
        const std::vector<bool> inRange{true, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, inRange) == -1);
        // ... while 2 stacks is under half and does qualify.
        const std::vector<int> light{5, 2};
        CHECK(TankSwapWanted(0, 5, 5, light, alive, inRange) == 1);
    }

    // The rotation wraps: with three tanks and the last one as victim, the main tank is the next one.
    {
        const std::vector<int> stacks{0, 0, 5};
        const std::vector<bool> alive{true, true, true};
        const std::vector<bool> inRange{true, true, true};
        CHECK(TankSwapWanted(2, 5, 5, stacks, alive, inRange) == 0);
    }

    // The swapped-off tank holds its threat for the window, or until its own stacks are gone, whichever comes first.
    {
        CHECK(TankSwapStandsDown(true, 3, 0));
        CHECK(TankSwapStandsDown(true, 3, kTankSwapStandDownMs - 1));
        CHECK(!TankSwapStandsDown(true, 3, kTankSwapStandDownMs));
        CHECK(!TankSwapStandsDown(true, 0, 1000));
        CHECK(!TankSwapStandsDown(false, 3, 1000));
    }

    std::cout << "All tank swap policy tests passed.\n";
    return 0;
}
