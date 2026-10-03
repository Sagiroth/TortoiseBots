#include "../ai/playerbot/AoeFearPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::AoeFearAllowed;

int main()
{
    std::cout << "Starting TortoiseBots AoE fear policy tests...\n";

    // The reported bug (issue #383): a hired holy priest in RFC (dungeon,
    // real player master) must not cast Psychic Scream.
    CHECK(!AoeFearAllowed(true, true));
    // A masterless pool bot inside an instance is equally unsafe: the next
    // pack is always near.
    CHECK(!AoeFearAllowed(true, false));

    // A bot grouped with a real player in the open world holds its fear too:
    // the pull belongs to the player.
    CHECK(!AoeFearAllowed(false, true));

    // Outside, with no master to disrupt, the old emergency behaviour stays.
    CHECK(AoeFearAllowed(false, false));

    std::cout << "All AoE fear policy checks PASSED!\n";
    return 0;
}
