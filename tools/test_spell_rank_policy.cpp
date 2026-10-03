#include "../ai/playerbot/SpellRankPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::SpellOverLevelForBot;
using ai::SpellRankTeachableNow;

int main()
{
    std::cout << "Starting TortoiseBots spell-rank policy tests...\n";

    // The reported bug (issue #381): a level-16 priest must not be taught
    // Power Word: Fortitude rank 3 (taught spell 1245, level 24).
    CHECK(!SpellRankTeachableNow(16, 24, true, true));
    // Rank 2 (taught spell 1244, level 12) is legitimately taught at 16.
    CHECK(SpellRankTeachableNow(16, 12, true, true));
    // Rank 1 (level 1) likewise.
    CHECK(SpellRankTeachableNow(16, 1, true, true));

    // The trainer rows that make the gate load-bearing: several rows carry a
    // lower reqLevel than the spell they teach, so the row alone would admit
    // the spell early. The taught spell's own level decides.
    CHECK(!SpellRankTeachableNow(8, 10, true, true));   // Crusader Strike 1 (row req 8)
    CHECK(SpellRankTeachableNow(10, 10, true, true));
    CHECK(!SpellRankTeachableNow(6, 10, true, true));   // Crusader Strike 1 at 6
    CHECK(!SpellRankTeachableNow(20, 22, true, true));   // Intimidating Shout (row req 20)
    CHECK(SpellRankTeachableNow(22, 22, true, true));
    CHECK(!SpellRankTeachableNow(14, 22, true, true));   // Cure Disease (row req 14)
    CHECK(!SpellRankTeachableNow(7, 35, true, true));    // Elemental Fury via shaman row (row req 6)
    CHECK(!SpellRankTeachableNow(30, 35, true, true));

    // A red trainer row is never rescued by the level: the row gate runs first.
    CHECK(!SpellRankTeachableNow(60, 60, true, false));
    CHECK(!SpellRankTeachableNow(16, 1, true, false));

    // Spells with no level in the data (racials rank 1, mounts, form/shapeshift
    // placeholders such as Aquatic Form's spellLevel-0 teaching shape) carry
    // no rank gate: the trainer state is the only filter.
    CHECK(SpellRankTeachableNow(16, 0, false, true));
    CHECK(SpellRankTeachableNow(1, 0, false, true));

    // Cleanup rule for the hire downgrade path: an over-level spell is never
    // legitimate, whatever taught it. Level-less spells are never pruned.
    CHECK(SpellOverLevelForBot(16, 24, true));
    CHECK(SpellOverLevelForBot(30, 35, true));
    CHECK(!SpellOverLevelForBot(30, 30, true));
    CHECK(!SpellOverLevelForBot(30, 24, true));
    CHECK(!SpellOverLevelForBot(1, 0, false));

    std::cout << "All spell rank policy checks PASSED!\n";
    return 0;
}
