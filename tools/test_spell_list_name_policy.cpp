#include "../ai/playerbot/policy/class/SpellListNamePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::SpellListHasName;

int main()
{
    std::cout << "Starting TortoiseBots spell-list name policy tests...\n";

    // The reported bug: "Polymorph: Pig" seen first must not hide "Polymorph".
    CHECK(!SpellListHasName(",Polymorph: Pig,Polymorph: Turtle,", "Polymorph"));
    CHECK(!SpellListHasName(",Flash Heal,Lesser Heal,", "Heal"));
    CHECK(!SpellListHasName(",Dire Bear Form,", "Bear Form"));

    // An exact name present is found.
    CHECK(SpellListHasName(",Polymorph,", "Polymorph"));
    CHECK(SpellListHasName(",Opening,Closing,Duel,", "Duel"));

    // Edge cases: empty csv, name at start/end, empty name.
    CHECK(!SpellListHasName(",", "Polymorph"));
    CHECK(!SpellListHasName("", "Polymorph"));
    CHECK(SpellListHasName(",Polymorph,Pig,", "Polymorph"));
    CHECK(SpellListHasName(",Pig,Polymorph,", "Polymorph"));
    CHECK(!SpellListHasName(",Duel,", ""));
    CHECK(SpellListHasName(",Greater Blessing of Kings,Blessing of Kings,", "Blessing of Kings"));
    CHECK(!SpellListHasName(",Greater Blessing of Kings,", "Blessing of Kings"));

    std::cout << "All spell list name policy checks PASSED!\n";
    return 0;
}
