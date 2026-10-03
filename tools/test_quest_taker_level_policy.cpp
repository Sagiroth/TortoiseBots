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

int main()
{
    std::cout << "Starting TortoiseBots quest-taker level gate tests...\n";

    // -------------------------------------------------------------
    // (1) Leave-the-valley deliveries wait: a level 1-2 pool bot must
    // not take or walk to the QuestLevel 5 end-of-valley hand-ins
    // (Dolanaar Delivery 2159, Rest and Relaxation 2158, Supplies to
    // Tannok 2160, A Peon's Burden 2161, A Task Unfinished 1656,
    // A Rogue's Deal 8) - the taker sits in the next town.
    // -------------------------------------------------------------
    {
        // Level-1 bot, QuestLevel 5 delivery: parked.
        CHECK(!QuestTakerTripFits(5, 5, 1, true));
        // Level-2 bot, same: still parked.
        CHECK(!QuestTakerTripFits(5, 5, 2, true));
        // Level-4 bot, QuestLevel 5: one above, allowed.
        CHECK(QuestTakerTripFits(5, 5, 4, true));
        // Level-5 bot, own level: allowed (the trip finally happens).
        CHECK(QuestTakerTripFits(5, 5, 5, true));
        std::cout << "  [PASS] QuestLevel 5 deliveries wait until level 4-5\n";
    }

    // -------------------------------------------------------------
    // (2) Normal valley work keeps working: a quest rated at most one
    // above the bot is taken and handed in, whatever the valley.
    // Live cases: A Threat Within (783, QuestLevel 1), Your Place In
    // The World (4641, QuestLevel 1), A Humble Task (752, QuestLevel 2).
    // -------------------------------------------------------------
    {
        // QuestLevel 1 hand-in at level 1: allowed.
        CHECK(QuestTakerTripFits(1, 1, 1, true));
        // QuestLevel 2 hand-in at level 1: one above, allowed.
        CHECK(QuestTakerTripFits(2, 2, 1, true));
        // QuestLevel 2 hand-in at level 2: allowed.
        CHECK(QuestTakerTripFits(2, 2, 2, true));
        // QuestLevel 3 hand-in at level 1 (Coldridge Mail 233): parked.
        CHECK(!QuestTakerTripFits(3, 3, 1, true));
        // Same at level 2: one above, allowed.
        CHECK(QuestTakerTripFits(3, 3, 2, true));
        std::cout << "  [PASS] valley quests within +1 keep working\n";
    }

    // -------------------------------------------------------------
    // (3) The taker area half: a quest rated inside the cap still waits
    // when its taker stands in an area rated higher (the next town),
    // and a same-valley taker keeps working.
    // -------------------------------------------------------------
    {
        // Low quest but taker area 5 for a level-1 bot: parked.
        CHECK(!QuestTakerTripFits(1, 5, 1, true));
        // Taker area one above: allowed.
        CHECK(QuestTakerTripFits(1, 2, 1, true));
        // Unknown taker area (0): never parked on the area half.
        CHECK(QuestTakerTripFits(5, 0, 1, true) == false); // quest half still parks
        CHECK(QuestTakerTripFits(1, 0, 1, true));
        std::cout << "  [PASS] over-level taker areas park the trip\n";
    }

    // -------------------------------------------------------------
    // (4) Exemptions: scaling quests, owned/hired bots, level 10+.
    // -------------------------------------------------------------
    {
        // QuestLevel 0 (scaling): never parked on the quest half.
        CHECK(QuestTakerTripFits(0, 5, 1, true) == false); // area half still parks
        CHECK(QuestTakerTripFits(0, 0, 1, true));
        // Owned/hired bot (not masterless random): player decides.
        CHECK(QuestTakerTripFits(5, 5, 1, false));
        CHECK(QuestTakerTripFits(60, 60, 2, false));
        CHECK(QuestTakerTripFits(5, 5, 9, true)); // level 9 outlevels a QuestLevel 5 hand-in: allowed
        std::cout << "  [PASS] scaling quests, owned bots and 10+ exempt\n";
    }

    std::cout << "All quest-taker level gate checks PASSED!\n";
    return 0;
}
