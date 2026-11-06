// Standalone policy test for the mage-portal follow: bots in a party follow
// their real-player master through a party mage's city portal, the caster
// going last, with the existing summon as fallback when the portal closes
// first. Mirrors ai/playerbot/PortalFollowPolicy.h; no server headers needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_portal_follow_policy.cpp -o /tmp/test_portal_follow && /tmp/test_portal_follow

#include "../ai/playerbot/PortalFollowPolicy.h"

#include <cstdint>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)
} // namespace

int main()
{
    // Eligible: real-player master on another map, alive, out of combat,
    // follow strategy, owned party bot.
    CHECK(ai::PortalFollowEligible(true, true, true, false, true, false));
    // Master a bot, same map, dead, fighting, stay/guard, or pool: no.
    CHECK(!ai::PortalFollowEligible(false, true, true, false, true, false));
    CHECK(!ai::PortalFollowEligible(true, false, true, false, true, false));
    CHECK(!ai::PortalFollowEligible(true, true, false, false, true, false));
    CHECK(!ai::PortalFollowEligible(true, true, true, true, true, false));
    CHECK(!ai::PortalFollowEligible(true, true, true, false, false, false));
    CHECK(!ai::PortalFollowEligible(true, true, true, false, true, true));

    // The caster waits while others still have to go, and for the short
    // delay after they went.
    CHECK(ai::PortalFollowMageWaits(true, 60000));
    CHECK(ai::PortalFollowMageWaits(false, 0));
    CHECK(ai::PortalFollowMageWaits(false, ai::kPortalFollowMageDelayMs - 1));
    CHECK(!ai::PortalFollowMageWaits(false, ai::kPortalFollowMageDelayMs));
    CHECK(!ai::PortalFollowMageWaits(false, 60000));

    // The window is bounded: once it runs out the summon fallback owns it.
    CHECK(!ai::PortalFollowWindowExpired(0));
    CHECK(!ai::PortalFollowWindowExpired(ai::kPortalFollowWindowMs - 1));
    CHECK(ai::PortalFollowWindowExpired(ai::kPortalFollowWindowMs));
    CHECK(ai::PortalFollowWindowExpired(ai::kPortalFollowWindowMs + 1000));

    // Reached within interact reach, not past it.
    CHECK(ai::PortalFollowReached(0.0f));
    CHECK(ai::PortalFollowReached(ai::kPortalFollowReachYd));
    CHECK(!ai::PortalFollowReached(ai::kPortalFollowReachYd + 0.1f));

    if (failures == 0)
        std::cout << "portal follow policy tests passed\n";
    return failures == 0 ? 0 : 1;
}
