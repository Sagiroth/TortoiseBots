#include "../ai/playerbot/ReadyRebuffPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ReadyRebuffAnchorKey;
using ai::ReadyRebuffCapSec;
using ai::ReadyRebuffDue;
using ai::ReadyRebuffGraceSec;

int main()
{
    std::cout << "Starting TortoiseBots ready-rebuff defer tests...\n";

    // Grace lets the buff pass run; the cap keeps the reply inside a normal
    // ready-check window so the pull never leaves without an answer.
    CHECK(ReadyRebuffGraceSec() == 8);
    CHECK(ReadyRebuffCapSec() == 30);
    CHECK(ReadyRebuffAnchorKey() == std::string("rebuff ready since"));
    std::cout << "  [PASS] grace, cap and anchor key are fixed\n";

    // No anchor: nothing deferred, never due.
    CHECK(!ReadyRebuffDue(0, 1000, false));
    CHECK(!ReadyRebuffDue(0, 1000, true));
    std::cout << "  [PASS] unanchored check never replies early\n";

    // Inside the grace window: hold even when idle, so buffs land first.
    CHECK(!ReadyRebuffDue(1000, 1000, false));
    CHECK(!ReadyRebuffDue(1000, 1000 + ReadyRebuffGraceSec() - 1, false));
    CHECK(!ReadyRebuffDue(1000, 1000 + 3, true));
    std::cout << "  [PASS] grace window holds the confirm\n";

    // Past grace and not casting: reply, buffs had their chance.
    CHECK(ReadyRebuffDue(1000, 1000 + ReadyRebuffGraceSec(), false));
    CHECK(ReadyRebuffDue(1000, 1000 + ReadyRebuffCapSec() - 1, false));
    std::cout << "  [PASS] settled bot replies after grace\n";

    // Past grace but mid-cast: keep holding, the buff is landing now.
    CHECK(!ReadyRebuffDue(1000, 1000 + ReadyRebuffGraceSec(), true));
    CHECK(!ReadyRebuffDue(1000, 1000 + ReadyRebuffCapSec() - 1, true));
    std::cout << "  [PASS] mid-cast hold past grace\n";

    // Past the cap: reply even mid-cast, so chained casts can never wedge
    // the check and leave the raid waiting on a bot that already answered.
    CHECK(ReadyRebuffDue(1000, 1000 + ReadyRebuffCapSec(), true));
    CHECK(ReadyRebuffDue(1000, 1000 + ReadyRebuffCapSec(), false));
    CHECK(ReadyRebuffDue(1000, 1000 + 300, true));
    std::cout << "  [PASS] cap always replies\n";

    std::cout << "All TortoiseBots ready-rebuff defer tests passed.\n";
    return 0;
}
