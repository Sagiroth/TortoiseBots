#include "../ai/playerbot/strategy/values/TradeTrainerPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TradeSpellJustifiesTrip;

int main()
{
    // Next rank or recipe for a held skill: always worth the walk.
    CHECK(TradeSpellJustifiesTrip(171, true, false, false));
    CHECK(TradeSpellJustifiesTrip(171, true, false, true));
    CHECK(TradeSpellJustifiesTrip(171, true, true, false));

    // First-rank primary with a free slot: the bot could take it up.
    CHECK(TradeSpellJustifiesTrip(182, false, true, true));

    // First-rank primary without a free slot: no third primary.
    CHECK(!TradeSpellJustifiesTrip(182, false, true, false));

    // Higher rank or recipe for an unheld skill: teaches nothing usable.
    CHECK(!TradeSpellJustifiesTrip(182, false, false, true));
    CHECK(!TradeSpellJustifiesTrip(182, false, false, false));

    // Unresolvable skill id: fail closed, no trip.
    CHECK(!TradeSpellJustifiesTrip(0, false, true, true));
    CHECK(!TradeSpellJustifiesTrip(0, true, false, true));

    std::cout << "trade trainer policy: OK\n";
    return 0;
}
