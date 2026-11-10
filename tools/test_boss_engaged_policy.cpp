// Standalone check for ai/playerbot/BossEngagedPolicy.h (AiPlayerbot.BossTacticsKeepsEngagedBoss):
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_engaged_policy.cpp -o /tmp/test_boss_engaged_policy && /tmp/test_boss_engaged_policy
#include "../ai/playerbot/BossEngagedPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

// A usable registry entry: known, fresh this tick, alive, in combat, not evading, the bot in combat and in reach.
static EngagedBossView Usable(std::uint32_t lastSeenMs)
{
    EngagedBossView view;
    view.known = true;
    view.lastSeenMs = lastSeenMs;
    view.alive = true;
    view.inCombat = true;
    view.evading = false;
    view.botInCombat = true;
    view.botToBossYd = 40.0f;
    return view;
}

int main()
{
    std::cout << "Starting TortoiseBots engaged boss policy tests...\n";

    // The entry lives for 10 s after the last bot saw the boss: 9,999 ms still counts, 10,000 does not, and a clock
    // wrap never makes a stale entry fresh.
    {
        CHECK(EngagedBossUsable(Usable(0), 0));
        CHECK(EngagedBossUsable(Usable(0), kEngagedBossFreshMs - 1));
        CHECK(!EngagedBossUsable(Usable(0), kEngagedBossFreshMs));
        CHECK(EngagedBossUsable(Usable(0xFFFFFFF0u), 0x00000010u));  // wrapped: 32 ms is fresh
        EngagedBossView unknown;
        CHECK(!EngagedBossUsable(unknown, 0));
    }

    // A bot that is not in combat itself never gains a boss (a bot outside the fight stays out).
    {
        EngagedBossView view = Usable(0);
        view.botInCombat = false;
        CHECK(!EngagedBossUsable(view, 0));
    }

    // The boss has to be alive, in combat, not evading, and within the reach (150 yd, 150.1 is too far).
    {
        EngagedBossView view = Usable(0);
        view.alive = false;
        CHECK(!EngagedBossUsable(view, 0));
        view = Usable(0);
        view.inCombat = false;
        CHECK(!EngagedBossUsable(view, 0));
        view = Usable(0);
        view.evading = true;
        CHECK(!EngagedBossUsable(view, 0));
        view = Usable(0);
        view.botToBossYd = kEngagedBossReachYd;
        CHECK(EngagedBossUsable(view, 0));
        view.botToBossYd = kEngagedBossReachYd + 0.1f;
        CHECK(!EngagedBossUsable(view, 0));
    }

    // The registry is written at most once per boss per half second; an entry nobody holds is always due.
    {
        CHECK(EngagedBossNoteDue(false, 0, 0));
        CHECK(EngagedBossNoteDue(false, 12345, 9999));
        CHECK(!EngagedBossNoteDue(true, 1000, 501));
        CHECK(EngagedBossNoteDue(true, 1000, 500));
        CHECK(!EngagedBossNoteDue(true, 0x00000050u, 0xFFFFFF00u));  // wrapped: 336 ms, not due
        CHECK(EngagedBossNoteDue(true, 0x00000200u, 0xFFFFFF00u));   // wrapped: 768 ms, due
    }

    std::cout << "All engaged boss policy tests passed.\n";
    return 0;
}
