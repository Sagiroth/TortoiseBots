#include "../ai/playerbot/QuestGiverStallPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kQuestGiverBackoffParkSec;
using ai::kQuestGiverStallBackoffAfter;
using ai::QuestGiverBackoffKey;
using ai::QuestGiverStallBacksOff;
using ai::QuestGiverStallKey;
using ai::QuestGiverStallStateChanged;

int main()
{
    std::cout << "Starting TortoiseBots quest-giver back-off tests...\n";

    // (1) Numbers: 2 stalls, 30-min park (same window as the stuck-hand-in
    // and objective-stall parks).
    {
        CHECK(kQuestGiverStallBackoffAfter == 2);
        CHECK(kQuestGiverBackoffParkSec == 30 * 60);
        std::cout << "  [PASS] back-off after 2 stalls, 30-min park\n";
    }

    // (2) Verdict: one stall only counts, the second engages, further ones
    // hold (the caller parks and clears on fire, so 3+ only matters if the
    // park lapsed and the pair stalled again).
    {
        CHECK(!QuestGiverStallBacksOff(0));
        CHECK(!QuestGiverStallBacksOff(1));
        CHECK(QuestGiverStallBacksOff(2));
        CHECK(QuestGiverStallBacksOff(3));
        std::cout << "  [PASS] verdict fires on the second stall\n";
    }

    // (3) Keys pin giver + quest: two quests at one NPC are independent, and
    // the same quest at two NPCs is independent too.
    {
        CHECK(QuestGiverStallKey(1518, 60113) != QuestGiverStallKey(1518, 367));
        CHECK(QuestGiverStallKey(1518, 60113) != QuestGiverStallKey(196, 60113));
        CHECK(QuestGiverBackoffKey(1518, 60113) != QuestGiverBackoffKey(1518, 367));
        CHECK(QuestGiverBackoffKey(1518, 60113) != QuestGiverBackoffKey(196, 60113));
        CHECK(QuestGiverStallKey(1518, 60113) == QuestGiverStallKey(1518, 60113));
        std::cout << "  [PASS] keys are per (giver, quest) pair\n";
    }

    // (4) State change breaks the chain: no stored state never counts as a
    // change (first stall anchors), an unchanged fingerprint keeps counting,
    // any change restarts.
    {
        CHECK(!QuestGiverStallStateChanged(0, false, 0));
        CHECK(!QuestGiverStallStateChanged(3, true, 3));
        CHECK(QuestGiverStallStateChanged(3, true, 1));
        CHECK(QuestGiverStallStateChanged(0, true, 3));
        std::cout << "  [PASS] quest-state change resets the count\n";
    }

    std::cout << "All quest-giver back-off checks PASSED!\n";
    return 0;
}
