#include "../ai/playerbot/LowbieGraveyardPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::LOWBIE_GRAVEYARD_MAX_LEVEL;
using ai::ShouldSendLowbieHome;
using ai::ShouldSkipDeathCountSpiritHeal;

int main()
{
    std::cout << "Starting TortoiseBots lowbie-graveyard policy tests...\n";

    // Brafostu (live 2026-10-03): level-2 Elwynn bot, 36 deaths to Mangy
    // Wolf / Defias Cutpurse / Forest Spider, never goes to the spirit
    // healer just for the count - the corpse run stays the default.
    CHECK(ShouldSkipDeathCountSpiritHeal(2, true));
    CHECK(ShouldSkipDeathCountSpiritHeal(9, true));
    std::cout << "  [PASS] sub-10 pool bot skips the death-count spirit heal\n";

    // Owned/hired bots keep today's behaviour: their player decides.
    CHECK(!ShouldSkipDeathCountSpiritHeal(2, false));
    std::cout << "  [PASS] owned/hired lowbie keeps the death-count gate\n";

    // Level 10+ keeps today's behaviour on both rules.
    CHECK(!ShouldSkipDeathCountSpiritHeal(10, true));
    CHECK(!ShouldSkipDeathCountSpiritHeal(40, true));
    CHECK(!ShouldSendLowbieHome(10, true, 8, 2));
    std::cout << "  [PASS] level 10+ keeps today's behaviour\n";

    // Brafostu woken at Goldshire (area 87:5) at level 2 goes home to
    // Northshire (area 9:2) via homebind teleport instead of walking.
    CHECK(ShouldSendLowbieHome(1, true, 5, 2));
    CHECK(ShouldSendLowbieHome(2, true, 5, 2));
    std::cout << "  [PASS] stranded level 1-2 town bot goes home\n";

    // A level 3+ bot in an area-5 town (5 <= level + 2) walks home itself.
    CHECK(!ShouldSendLowbieHome(3, true, 5, 2));
    CHECK(!ShouldSendLowbieHome(6, true, 5, 3));
    std::cout << "  [PASS] level 3+ town bot walks home itself\n";

    // Owned/hired bots are their player's business: never yanked.
    CHECK(!ShouldSendLowbieHome(2, false, 5, 2));
    CHECK(!ShouldSendLowbieHome(9, false, 8, 2));
    std::cout << "  [PASS] owned/hired bot never auto-sent home\n";

    // Fail closed: unknown area levels on either half refuse.
    CHECK(!ShouldSendLowbieHome(2, true, -1, 2));
    CHECK(!ShouldSendLowbieHome(2, true, 5, -2));
    CHECK(!ShouldSendLowbieHome(2, true, 0, 2));
    CHECK(!ShouldSendLowbieHome(2, true, 5, 0));
    std::cout << "  [PASS] unknown area level fails closed\n";

    // Fail closed: a mis-set homebind in an area above the bot refuses, so
    // the rescue can never bounce the bot somewhere worse.
    CHECK(!ShouldSendLowbieHome(2, true, 5, 8));
    CHECK(!ShouldSendLowbieHome(2, true, 8, 8));
    std::cout << "  [PASS] unfit homebind refuses\n";

    // Boundary: the area must rate STRICTLY above level + 2.
    CHECK(!ShouldSendLowbieHome(3, true, 5, 2));
    CHECK(ShouldSendLowbieHome(2, true, 5, 2));
    CHECK(ShouldSendLowbieHome(2, true, 6, 2));
    std::cout << "  [PASS] margin boundary is strict\n";

    // All six start valleys route through this one rule (town area 5,
    // home valley 2-7): spot-check a Mulgore and a Teldrassil pair.
    CHECK(ShouldSendLowbieHome(2, true, 5, 4));   // Bloodhoof 222:5, Mesa 220:4
    CHECK(!ShouldSendLowbieHome(2, true, 5, 7));  // Dolanaar 186:5, Aldrassil 256:7 refuses
    std::cout << "  [PASS] valley pairs follow the same rule\n";

    std::cout << "lowbie-graveyard policy: OK\n";
    return 0;
}
