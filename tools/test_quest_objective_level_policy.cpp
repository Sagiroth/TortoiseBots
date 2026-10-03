#include "../ai/playerbot/PullRegenPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::QuestObjectiveLevelFits;

int main()
{
    std::cout << "Starting TortoiseBots quest-objective level gate tests...\n";

    // -------------------------------------------------------------
    // (1) Pool bot below 10: the grind cap (+1) bounds quest spawns.
    // Live case: level-2 Elwynn bot on the item-750 trip (entry 69,
    // level_max 2) must keep its own spawns but refuse the level 5-6
    // neighbours sharing the field (Defias Cutpurse, Mangy Wolf,
    // Forest Spider).
    // -------------------------------------------------------------
    {
        // Own-level quest spawn: destination.
        CHECK(QuestObjectiveLevelFits(2, 2, true, false));
        // One above: still inside the cap.
        CHECK(QuestObjectiveLevelFits(3, 2, true, false));
        // Two above: parked, not walked to.
        CHECK(!QuestObjectiveLevelFits(4, 2, true, false));
        CHECK(!QuestObjectiveLevelFits(5, 2, true, false));
        CHECK(!QuestObjectiveLevelFits(6, 2, true, false));
        std::cout << "  [PASS] sub-10 pool quest spawns keep the +1 cap\n";
    }

    // -------------------------------------------------------------
    // (2) The ceiling travels with the bot, like the order cap.
    // -------------------------------------------------------------
    {
        // Level-4 bot: level 5 is fair game, level 6+ is not.
        CHECK(QuestObjectiveLevelFits(5, 4, true, false));
        CHECK(!QuestObjectiveLevelFits(6, 4, true, false));
        // Level 9: cap is still +1 just below 10.
        CHECK(QuestObjectiveLevelFits(10, 9, true, false));
        CHECK(!QuestObjectiveLevelFits(11, 9, true, false));
        std::cout << "  [PASS] ceiling travels with the bot below 10\n";
    }

    // -------------------------------------------------------------
    // (3) From level 10 the long-standing +4 ceiling stands, same as
    // the grind order cap (donor mod-playerbots shape).
    // -------------------------------------------------------------
    {
        CHECK(QuestObjectiveLevelFits(14, 10, true, false));
        CHECK(!QuestObjectiveLevelFits(15, 10, true, false));
        std::cout << "  [PASS] +4 ceiling from level 10\n";
    }

    // -------------------------------------------------------------
    // (4) Exemptions: vendors need no fight, owned/hired bots follow
    // their player.
    // -------------------------------------------------------------
    {
        // Vendor selling the quest item at any level: always a destination.
        CHECK(QuestObjectiveLevelFits(60, 2, true, true));
        // Owned/hired bot (not masterless random): player decides.
        CHECK(QuestObjectiveLevelFits(6, 2, false, false));
        CHECK(QuestObjectiveLevelFits(60, 2, false, false));
        std::cout << "  [PASS] vendors and owned bots are exempt\n";
    }

    std::cout << "All quest-objective level gate checks PASSED!\n";
    return 0;
}
