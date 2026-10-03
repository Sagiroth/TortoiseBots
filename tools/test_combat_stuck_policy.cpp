#include "../ai/playerbot/CombatStuckPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CombatStuckBlacklistExpiry;
using ai::COMBAT_STUCK_BLACKLIST_MS;
using ai::CombatStuckGiveUpReason;
using ai::ShouldBlacklistCombatStuckEntry;
using ai::ShouldGiveUpCombatStuckTarget;

int main()
{
    std::cout << "Starting TortoiseBots combat-stuck policy tests...\n";

    // The give-up window mirrors the reach action's five minutes, so a wedged
    // mob stays blacklisted exactly as long and the next grind pick walks on.
    CHECK(COMBAT_STUCK_BLACKLIST_MS == 5u * 60u * 1000u);
    CHECK(CombatStuckBlacklistExpiry(1000u) == 1000u + 5u * 60u * 1000u);
    CHECK(CombatStuckBlacklistExpiry(0u) == COMBAT_STUCK_BLACKLIST_MS);
    std::cout << "  [PASS] blacklist window is five minutes\n";

    // A live hostile is given up on; nothing, a corpse (looted, not
    // blacklisted), or the bot itself never is.
    CHECK(ShouldGiveUpCombatStuckTarget(true, true, false));
    CHECK(!ShouldGiveUpCombatStuckTarget(false, true, false));
    CHECK(!ShouldGiveUpCombatStuckTarget(false, false, false));
    CHECK(!ShouldGiveUpCombatStuckTarget(true, false, false));
    CHECK(!ShouldGiveUpCombatStuckTarget(true, true, true));
    std::cout << "  [PASS] give-up needs a live non-self target\n";

    // Only creature kinds are blacklisted: the entry list is keyed by
    // creature entry and drops grind destinations.
    CHECK(ShouldBlacklistCombatStuckEntry(true));
    CHECK(!ShouldBlacklistCombatStuckEntry(false));
    std::cout << "  [PASS] entry blacklist is creatures only\n";

    // ReachGiveUp funnel: same event the dashboard already counts, separable
    // by reason from the reach action's own rows.
    CHECK(CombatStuckGiveUpReason() == "combat-stuck");
    std::cout << "  [PASS] give-up logs under ReachGiveUp\n";

    std::cout << "combat-stuck policy: OK\n";
    return 0;
}
