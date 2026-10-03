#include "../ai/playerbot/GrindSpotPolicy.h"
#include "../ai/playerbot/PullRegenPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::QuestTakerTripFits;
using ai::QuestValleyExempted;

int main()
{
    std::cout << "Starting TortoiseBots quest-taker level gate tests...\n";

    // -------------------------------------------------------------
    // (1) Leave-the-valley deliveries wait: the taker stands in another
    // area (the next town) and the quest is above the bot's level
    // (Dolanaar Delivery 2159 and Rest and Relaxation 2158 are QuestLevel 5).
    // -------------------------------------------------------------
    {
        CHECK(!QuestTakerTripFits(5, true, 2, true));   // level 2: waits
        CHECK(!QuestTakerTripFits(5, true, 4, true));   // level 4: still below the quest
        CHECK(QuestTakerTripFits(5, true, 5, true));    // level 5: the trip happens
        std::cout << "  [PASS] other-area hand-ins wait for the quest level\n";
    }

    // -------------------------------------------------------------
    // (2) Valley work keeps working: same-area hand-ins up to +3, whatever
    // the area data says about the valley's level.
    // -------------------------------------------------------------
    {
        CHECK(QuestTakerTripFits(1, false, 1, true));
        CHECK(QuestTakerTripFits(3, false, 1, true));   // Coldridge-style QuestLevel 3 at level 1
        CHECK(QuestTakerTripFits(4, false, 1, true));   // +3
        CHECK(!QuestTakerTripFits(5, false, 1, true));  // +4: not taken yet
        CHECK(QuestTakerTripFits(2, true, 2, true));    // other area but quest at own level
        std::cout << "  [PASS] valley quests up to +3 keep working\n";
    }

    // -------------------------------------------------------------
    // (3) Exemptions: scaling quests, owned/hired bots, level 10+.
    // -------------------------------------------------------------
    {
        CHECK(QuestTakerTripFits(0, true, 1, true));    // scaling quest
        CHECK(QuestTakerTripFits(9, true, 1, false));   // owned/hired: player decides
        CHECK(QuestTakerTripFits(30, true, 10, true));  // level 10+: unchanged
        std::cout << "  [PASS] scaling quests, owned bots and 10+ exempt\n";
    }

    // -------------------------------------------------------------
    // (4) Quest valley exemption: a level 1-4 masterless bot skips the
    // destination area-average ceiling on quest objectives and givers,
    // same scope as the grind valley exemption. Live case: a level-1
    // tauren holding The Hunt Begins (QuestLevel 2) at Camp Narache
    // whose Plainstrider points sit in area level 6 - the ceiling vetoed
    // every point and the quest search parked with an empty list
    // (QuestTripNoTarget '0'), while the quest's own gates (+1 level
    // window, spawn template, point danger) already vet the trip.
    // -------------------------------------------------------------
    {
        CHECK(QuestValleyExempted(1, true));
        CHECK(QuestValleyExempted(4, true));
        CHECK(!QuestValleyExempted(5, true));
        CHECK(!QuestValleyExempted(1, false));
        CHECK(!QuestValleyExempted(60, true));
        std::cout << "  [PASS] level 1-4 masterless bots skip the quest valley ceiling\n";
    }

    std::cout << "All quest-taker level gate checks PASSED!\n";
    return 0;
}
