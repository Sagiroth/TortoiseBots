#include "../ai/playerbot/policy/loot/BlockedTellPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <utility>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BlockedTellAllowed;
using ai::BlockedTellOwner;
using ai::kBlockedTellCooldownSeconds;

int main()
{
    std::cout << "Starting TortoiseBots blocked-tell policy tests...\n";

    std::map<std::pair<uint64_t, uint64_t>, time_t> told;
    uint64_t const playerA = 0x0000000000001234;
    uint64_t const playerB = 0x0000000000005678;
    uint64_t const object = 0x0000000000000ABC;
    uint32_t const group = 7;

    // Two real players in one group, with bots reporting the same object:
    // the group is warned once, whoever's companion notices.
    CHECK(BlockedTellOwner(playerA, group) == BlockedTellOwner(playerB, group));
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerA, group), object, 1000) == true);
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerB, group), object, 1005) == false);
    std::cout << "  [PASS] one whisper per group and object\n";

    // Another object, or another group, is a separate warning.
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerB, group), object + 1, 1010) == true);
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerA, group + 1), object, 1010) == true);
    std::cout << "  [PASS] objects and groups keep separate records\n";

    // Outside a group each requester has its own record.
    CHECK(BlockedTellOwner(playerA, 0) != BlockedTellOwner(playerB, 0));
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerA, 0), object, 2000) == true);
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerB, 0), object, 2000) == true);
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerA, 0), object, 2005) == false);
    std::cout << "  [PASS] ungrouped requesters keep their own record\n";

    // The tenth minute ends the cooldown; earlier does not.
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerA, group), object,
        1000 + kBlockedTellCooldownSeconds - 1) == false);
    CHECK(BlockedTellAllowed(told, BlockedTellOwner(playerA, group), object,
        1000 + kBlockedTellCooldownSeconds) == true);
    std::cout << "  [PASS] the warning repeats after ten minutes\n";

    // A group id can never collide with a player guid.
    CHECK(BlockedTellOwner(1, group) != BlockedTellOwner(1, 0));
    CHECK(BlockedTellOwner(1, 0) == 1);
    std::cout << "  [PASS] group and player keys stay distinct\n";

    std::cout << "All blocked-tell policy checks PASSED!\n";
    return 0;
}
