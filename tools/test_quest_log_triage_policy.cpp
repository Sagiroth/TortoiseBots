#include "../ai/playerbot/QuestLogPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::QuestTriageShouldDrop;

int main()
{
    std::cout << "Starting TortoiseBots quest-log triage tests...\n";

    // (1) FAILED quests: dropped (donor OrganizeQuestLog drops FAILED).
    {
        CHECK(QuestTriageShouldDrop(10, 10, 0, 0, true, 0, 0));
        std::cout << "  [PASS] FAILED quests drop\n";
    }

    // (2) Over-level: donor IsQuestCapableDoing refuses botLevel + 3 <
    // questLevel. Scaling (0) never over-level.
    {
        CHECK(QuestTriageShouldDrop(14, 10, 0, 0, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(13, 10, 0, 0, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(0, 10, 0, 0, false, 0, 0));
        std::cout << "  [PASS] +3 over-level gate, scaling exempt\n";
    }

    // (3) Type != 0: elite/dungeon/raid/world-event picks are dropped;
    // solo (0) kept.
    {
        CHECK(QuestTriageShouldDrop(10, 10, 1, 0, false, 0, 0));
        CHECK(QuestTriageShouldDrop(10, 10, 81, 0, false, 0, 0));
        CHECK(QuestTriageShouldDrop(10, 10, 62, 0, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 0, 0));
        std::cout << "  [PASS] elite/dungeon/raid dropped, solo kept\n";
    }

    // (4) Suggested-group: donor refuses suggested >= 2.
    {
        CHECK(QuestTriageShouldDrop(10, 10, 0, 3, false, 0, 0));
        CHECK(QuestTriageShouldDrop(10, 10, 0, 2, false, 0, 0));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 1, false, 0, 0));
        std::cout << "  [PASS] group-suggested dropped, solo kept\n";
    }

    // (5) Zone mismatch: ZoneOrSort > 0 is a zone id - dropped when the
    // bot is elsewhere; sort ids (<= 0) and unknown bot zone (0) never
    // drop. The bot's own zone is kept.
    {
        CHECK(QuestTriageShouldDrop(10, 10, 0, 0, false, 14, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 12, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, -1, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 0, 12));
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 14, 0));
        std::cout << "  [PASS] zone mismatch dropped, sorts/unknown kept\n";
    }

    // (6) A normal doable quest survives every gate together.
    {
        CHECK(!QuestTriageShouldDrop(10, 10, 0, 0, false, 12, 12));
        std::cout << "  [PASS] doable quest kept\n";
    }

    std::cout << "All quest-log triage checks PASSED!\n";
    return 0;
}
