#include "../ai/playerbot/PullJoinWindowPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

namespace
{
bool StrEq(char const* a, char const* b)
{
    if (!a || !b)
        return a == b;
    return std::strcmp(a, b) == 0;
}
} // namespace

int main()
{
    // 1. Pre-pull hold: requester alive, no combat, before timeout -> keep holding.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = true;
        f.elapsedSinceOrder = 10;
        CHECK(ai::EngageReleaseReason(f) == nullptr);
        CHECK(!ai::EngageReleases(f));
    }

    // 2. Requester enters combat: hold during the delay window, release at delaySeconds.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = true;
        f.requesterInCombat = true;
        f.combatStarted = true;
        f.elapsedSinceOrder = 12;
        f.elapsedSinceCombat = 3;
        f.delaySeconds = 4;
        CHECK(ai::EngageReleaseReason(f) == nullptr);

        f.elapsedSinceCombat = 4;
        CHECK(StrEq(ai::EngageReleaseReason(f), "delay"));
        CHECK(ai::EngageReleases(f));
    }

    // 3. Zero-second delay releases as soon as requester enters combat.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = true;
        f.requesterInCombat = true;
        f.combatStarted = true;
        f.elapsedSinceCombat = 0;
        f.delaySeconds = 0;
        CHECK(StrEq(ai::EngageReleaseReason(f), "delay"));
    }

    // 4. Mob reaches melee range of requester or tank before delay expires -> immediate release.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = true;
        f.requesterInCombat = true;
        f.combatStarted = true;
        f.elapsedSinceCombat = 1;
        f.delaySeconds = 4;
        f.mobInMelee = true;
        CHECK(StrEq(ai::EngageReleaseReason(f), "melee"));
    }

    // 5. Self-defence: a bot directly attacked breaks out immediately.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = true;
        f.botIsVictim = true;
        CHECK(StrEq(ai::EngageReleaseReason(f), "hit"));
    }

    // 6. Nothing pulled: expires on timeout.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = true;
        f.elapsedSinceOrder = 59;
        f.timeoutSeconds = 60;
        CHECK(ai::EngageReleaseReason(f) == nullptr);

        f.elapsedSinceOrder = 60;
        CHECK(StrEq(ai::EngageReleaseReason(f), "timeout"));
    }

    // 7. Requester dies, logs out or changes map -> release.
    {
        ai::EngageReleaseFacts f;
        f.requesterAlive = false;
        CHECK(StrEq(ai::EngageReleaseReason(f), "requester-gone"));
    }

    // 8. PullJoinWindow engage lifecycle.
    {
        ai::PullJoinWindow w;
        w.StartEngage(100, 0xF, 10, 42ULL, 4, 60);
        CHECK(w.Running());
        CHECK(w.engage);
        CHECK(w.engageRequesterGuid == 42ULL);
        CHECK(w.engageDelaySeconds == 4);
        CHECK(w.engageTimeoutSeconds == 60);
        CHECK(w.engageCombatStart == 0);
        CHECK(w.savedWaitSeconds == 10);

        w.MarkEngageCombatStarted(105);
        CHECK(w.engageCombatStart == 105);
        w.MarkEngageCombatStarted(108);
        CHECK(w.engageCombatStart == 105);

        w.Clear();
        CHECK(!w.Running());
        CHECK(!w.engage);
        CHECK(w.engageRequesterGuid == 0);
        CHECK(w.engageCombatStart == 0);
    }

    std::cout << "test_engage_policy: ALL PASSED\n";
    return 0;
}
