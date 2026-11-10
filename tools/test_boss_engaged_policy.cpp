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

    // EngagedBossRegistry_InstanceBucketsIsolated: a note, a read and the expiry sweep of one map instance never visit,
    // return or expire another instance's entries, and a bucket whose last entry expires goes with it. A read is
    // passed the reading bot's clock, so a stale row of another instance is only ever judged by that instance's bots.
    {
        const std::vector<std::uint32_t> none;
        EngagedBossRegistry registry;
        registry.Note(249, 1, 10184, 0x1000, 0);
        registry.Note(249, 2, 10184, 0x2000, 0);
        CHECK(registry.size == 2 && registry.buckets.size() == 2);

        // A second note inside the half second is not due: the earlier guid and time stay.
        registry.Note(249, 1, 10184, 0x9999, 300);
        std::vector<EngagedBossNote> one = registry.Read(249, 1, 300, none);
        CHECK(one.size() == 1 && one[0].guid == 0x1000 && one[0].lastSeenMs == 0);

        // Instance 1 refreshes its row at 60 s; instance 2's row, long stale by that clock, is still returned by
        // instance 2's read at its own clock 0: instance 1's note never expired it.
        registry.Note(249, 1, 10184, 0x1001, 60000);
        CHECK(registry.size == 2 && registry.buckets.size() == 2);
        one = registry.Read(249, 1, 60000, none);
        CHECK(one.size() == 1 && one[0].guid == 0x1001 && one[0].lastSeenMs == 60000);
        std::vector<EngagedBossNote> two = registry.Read(249, 2, 0, none);
        CHECK(two.size() == 1 && two[0].guid == 0x2000 && two[0].lastSeenMs == 0);

        // Reads return the caller's instance only, and skip the entries the caller already read.
        CHECK(registry.Read(249, 1, 60000, std::vector<std::uint32_t>(1, 10184)).empty());
        CHECK(registry.Read(249, 3, 60000, none).empty());
        CHECK(registry.Read(0, 1, 60000, none).empty());

        // A second boss in instance 1: its own bucket grows, instance 2's row is still there.
        registry.Note(249, 1, 15000, 0x3000, 61000);
        CHECK(registry.size == 3 && registry.buckets.size() == 2);
        CHECK(registry.Read(249, 1, 61000, none).size() == 2);
        two = registry.Read(249, 2, 0, none);
        CHECK(two.size() == 1 && two[0].guid == 0x2000 && two[0].lastSeenMs == 0);

        // An instance 1 note at 95 s expires its own 15000 row and keeps the bucket (10184 is fresh); instance 2's
        // row survives both the sweep and the size.
        registry.Note(249, 1, 10184, 0x1002, 95000);
        CHECK(registry.size == 2 && registry.buckets.size() == 2);
        one = registry.Read(249, 1, 95000, std::vector<std::uint32_t>(1, 10184));
        CHECK(one.empty());  // 15000 went, the refreshed 10184 is the entry the caller read
        two = registry.Read(249, 2, 0, none);
        CHECK(two.size() == 1 && two[0].guid == 0x2000 && two[0].lastSeenMs == 0);

        // Instance 2's own read at 95 s drops its stale row and, with it, the empty bucket.
        two = registry.Read(249, 2, 95000, none);
        CHECK(two.empty());
        CHECK(registry.size == 1 && registry.buckets.size() == 1);

        // Instance 1's last row expires on its next read: its bucket goes too.
        one = registry.Read(249, 1, 130000, none);
        CHECK(one.empty());
        CHECK(registry.size == 0 && registry.buckets.size() == 0);
    }

    std::cout << "All engaged boss policy tests passed.\n";
    return 0;
}
