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
        const std::vector<bool> canTaunt{true, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, inRange, canTaunt) == 1);
    }

    // SkipsCandidateWithoutTaunt: a rotation tank with no taunt cannot take the boss: no trained taunt at all, so the
    // next tank in the rotation is chosen instead of a tank that would fire the trigger every tick with nothing to
    // cast.
    {
        const std::vector<int> stacks{5, 0, 0};
        const std::vector<bool> alive{true, true, true};
        const std::vector<bool> inRange{true, true, true};
        const std::vector<bool> canTaunt{true, false, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, inRange, canTaunt) == 2);
        // Nobody with a taunt: no swap at all.
        const std::vector<bool> none{true, false, false};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, inRange, none) == -1);
    }

    // Below the row's threshold nobody swaps, however clean the other tank is.
    {
        const std::vector<int> stacks{4, 0};
        const std::vector<bool> alive{true, true};
        const std::vector<bool> inRange{true, true};
        const std::vector<bool> canTaunt{true, true};
        CHECK(TankSwapWanted(0, 4, 5, stacks, alive, inRange, canTaunt) == -1);
        CHECK(TankSwapWanted(0, 0, 5, stacks, alive, inRange, canTaunt) == -1);
    }

    // A dead or out-of-reach tank is skipped, and the search still finds the next ready one past it.
    {
        const std::vector<int> stacks{5, 0, 0};
        const std::vector<bool> dead{true, false, true};
        const std::vector<bool> alive{true, true, true};
        const std::vector<bool> inRange{true, true, true};
        const std::vector<bool> canTaunt{true, true, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, dead, inRange, canTaunt) == 2);
        const std::vector<bool> far{true, false, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, far, canTaunt) == 2);
        // Nobody ready: no swap rather than a taunt from a corpse or from across the room.
        const std::vector<bool> allDead{true, false, false};
        CHECK(TankSwapWanted(0, 5, 5, stacks, allDead, inRange, canTaunt) == -1);
    }

    // A candidate that still carries a deep stack is not a swap: at 3 of a threshold of 5 it would only add stacks
    // faster (fewer than half the threshold, 2.5, is the bar).
    {
        const std::vector<int> stacks{5, 3};
        const std::vector<bool> alive{true, true};
        const std::vector<bool> inRange{true, true};
        const std::vector<bool> canTaunt{true, true};
        CHECK(TankSwapWanted(0, 5, 5, stacks, alive, inRange, canTaunt) == -1);
        // ... while 2 stacks is under half and does qualify.
        const std::vector<int> light{5, 2};
        CHECK(TankSwapWanted(0, 5, 5, light, alive, inRange, canTaunt) == 1);
    }

    // The rotation wraps: with three tanks and the last one as victim, the main tank is the next one.
    {
        const std::vector<int> stacks{0, 0, 5};
        const std::vector<bool> alive{true, true, true};
        const std::vector<bool> inRange{true, true, true};
        const std::vector<bool> canTaunt{true, true, true};
        CHECK(TankSwapWanted(2, 5, 5, stacks, alive, inRange, canTaunt) == 0);
    }

    // The swapped-off tank holds its threat for the window, or until its own stacks are gone, whichever comes first.
    {
        CHECK(TankSwapStandsDown(true, 3, 0, true));
        CHECK(TankSwapStandsDown(true, 3, kTankSwapStandDownMs - 1, true));
        CHECK(!TankSwapStandsDown(true, 3, kTankSwapStandDownMs, true));
        CHECK(!TankSwapStandsDown(true, 0, 1000, true));
        CHECK(!TankSwapStandsDown(false, 3, 1000, true));
        // The hold ends on the stack drop at once, whatever the window still allows.
        CHECK(!TankSwapHoldsAction(TankSwapStandsDown(true, 0, 1000, true), false, false, true));
        CHECK(!TankSwapHoldsAction(TankSwapStandsDown(true, 0, 1000, true), true, false, true));
        CHECK(!TankSwapHoldsAction(TankSwapStandsDown(true, 0, 1000, true), false, true, false));
    }

    // StandDownEndsWhenNewTankLosesBoss: the stand-down lasts only while the new tank actually holds the boss: a
    // resisted taunt, a dead new tank or a boss on a healer ends it at once, and the old tank may taunt again (the
    // rescue).
    {
        CHECK(!TankSwapStandsDown(true, 3, 0, false));
        CHECK(!TankSwapStandsDown(true, 3, kTankSwapStandDownMs - 1, false));
        // A held taunt stays held while the new tank holds, so the rule itself did not start letting taunts through.
        CHECK(TankSwapHoldsAction(TankSwapStandsDown(true, 3, 0, true), true, false, false));
        CHECK(!TankSwapHoldsAction(TankSwapStandsDown(true, 3, 0, false), true, false, false));
    }

    // The hold names no class: the rule answers from what the action is aimed at, whether it is a taunt and whether it
    // damages an area, so every tank's boss action is held and single-target actions aimed elsewhere still run.
    {
        const bool down = TankSwapStandsDown(true, 5, 0, true);
        struct Case { const char* action; bool taunt; bool area; bool targetsBoss; bool held; };
        const Case cases[] = {
            { "growl", true, false, true, true },             // a druid's taunt, aimed at the boss
            { "maul", false, false, true, true },             // a druid's threat builder, aimed at the boss
            { "heroic strike", false, false, true, true },    // a warrior's rotation, as before
            { "sunder armor", false, false, true, true },
            { "revenge", false, false, true, true },
            { "mocking blow", true, false, false, true },     // a taunt is held whatever the tank picked
            { "challenging roar", true, false, false, true }, // an area taunt reaches the boss from any pick
            { "consecration", false, true, false, true },     // a paladin's area cast reaches the boss too
            { "swipe", false, true, false, true },            // ... as does a druid's cleave on an add
            { "holy light", false, false, false, false },     // a paladin's heal is not the boss
            { "faerie fire (feral)", false, false, false, false },  // the same druid's cast on an add
        };
        for (const Case& c : cases)
            CHECK(TankSwapHoldsAction(down, c.taunt, c.area, c.targetsBoss) == c.held);
        // Outside the stand-down nothing is held, whatever the action.
        CHECK(!TankSwapHoldsAction(false, true, false, true));
        CHECK(!TankSwapHoldsAction(false, false, false, true));
        CHECK(!TankSwapHoldsAction(false, false, true, false));
    }

    std::cout << "All tank swap policy tests passed.\n";
    return 0;
}
