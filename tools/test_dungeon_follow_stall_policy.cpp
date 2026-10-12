#include "../ai/playerbot/DungeonFollowStallPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DungeonFollowStallWatch;
using ai::UseDungeonFollowWatch;

// Dungeon follow range (raid follow distance) as the module uses it.
static float const kRange = 5.0f;

// Feeds the watch every 100 ms from `from` to `to` (inclusive) at one spot.
static bool StandStill(DungeonFollowStallWatch& watch, uint32_t from, uint32_t to, float x, float y,
    float leaderDistance, bool held = false)
{
    bool rescuing = false;
    for (uint32_t t = from; t <= to; t += 100)
        rescuing = watch.Observe(t, x, y, leaderDistance, kRange, held);
    return rescuing;
}

int main()
{
    std::cout << "Starting TortoiseBots dungeon follow stall policy tests...\n";

    // Gate: dungeon and out of combat only.
    {
        CHECK(UseDungeonFollowWatch(true, false));
        CHECK(!UseDungeonFollowWatch(false, false));
        CHECK(!UseDungeonFollowWatch(true, true));
        std::cout << "  [PASS] gate: dungeon, out of combat\n";
    }

    // Uldaman island edge: 35 yd behind, not a yard of motion. Rescue after
    // two seconds, not before.
    {
        DungeonFollowStallWatch watch;
        CHECK(!StandStill(watch, 10000, 11900, -257.1f, 95.5f, 35.0f));
        CHECK(StandStill(watch, 12000, 12000, -257.1f, 95.5f, 35.0f));
        CHECK(watch.rescuing);
        std::cout << "  [PASS] stall: 2 s still while 35 yd behind\n";
    }

    // A bot that keeps walking behind its leader is not stalled.
    {
        DungeonFollowStallWatch watch;
        bool rescuing = false;
        float x = 0.0f;
        for (uint32_t t = 0; t <= 10000; t += 100)
        {
            x += 0.7f; // ~7 yd/s
            rescuing = watch.Observe(t, x, 0.0f, 30.0f, kRange, false);
            CHECK(!rescuing);
        }
        std::cout << "  [PASS] walking behind is not a stall\n";
    }

    // Within follow range plus the slack (8 yd) standing is normal; it also
    // ends a running rescue.
    {
        DungeonFollowStallWatch watch;
        CHECK(!StandStill(watch, 0, 5000, 0.0f, 0.0f, 8.0f));
        CHECK(StandStill(watch, 6000, 9000, 0.0f, 0.0f, 20.0f));
        CHECK(!watch.Observe(9100, 0.0f, 0.0f, 7.9f, kRange, false));
        CHECK(!watch.rescuing);
        std::cout << "  [PASS] within range: no stall, and a rescue ends\n";
    }

    // Eating, drinking, casting or rooted: standing still on purpose.
    {
        DungeonFollowStallWatch watch;
        CHECK(!StandStill(watch, 0, 10000, 0.0f, 0.0f, 30.0f, true));
        std::cout << "  [PASS] held in place is not a stall\n";
    }

    // Observations far apart (the bot did other things in between) restart
    // the stand-still clock instead of adding the gap.
    {
        DungeonFollowStallWatch watch;
        CHECK(!watch.Observe(0, 0.0f, 0.0f, 30.0f, kRange, false));
        CHECK(!watch.Observe(1500, 0.0f, 0.0f, 30.0f, kRange, false));
        CHECK(!watch.Observe(3000, 0.0f, 0.0f, 30.0f, kRange, false));
        CHECK(!StandStill(watch, 3100, 4900, 0.0f, 0.0f, 30.0f));
        CHECK(StandStill(watch, 5000, 5000, 0.0f, 0.0f, 30.0f));
        std::cout << "  [PASS] a feed gap restarts the clock\n";
    }

    // A rescue stays on while its walk moves the bot, until it catches up.
    {
        DungeonFollowStallWatch watch;
        CHECK(StandStill(watch, 0, 2000, 0.0f, 0.0f, 40.0f));
        CHECK(watch.Observe(2100, 3.0f, 0.0f, 37.0f, kRange, false));
        CHECK(watch.Observe(6000, 30.0f, 0.0f, 10.0f, kRange, false));
        CHECK(!watch.Observe(6500, 33.0f, 0.0f, 6.0f, kRange, false));
        std::cout << "  [PASS] rescue latches until the bot is back in range\n";
    }

    // Path walks: one at once, then at most one per 2 s; a walk that cannot
    // start doubles the wait up to 8 s; a started walk resets it.
    {
        DungeonFollowStallWatch watch;
        CHECK(!watch.WalkDue(0)); // not rescuing
        CHECK(StandStill(watch, 0, 2000, 0.0f, 0.0f, 40.0f));
        CHECK(watch.WalkDue(2000));
        watch.Walked(2000, true);
        CHECK(!watch.WalkDue(3999));
        CHECK(watch.WalkDue(4000));
        watch.Walked(4000, false);
        CHECK(watch.WalkInterval() == 4000);
        CHECK(!watch.WalkDue(7999));
        CHECK(watch.WalkDue(8000));
        watch.Walked(8000, false);
        CHECK(watch.WalkInterval() == 8000);
        watch.Walked(16000, false);
        watch.Walked(24000, false);
        CHECK(watch.WalkInterval() == 8000);
        watch.Walked(32000, true);
        CHECK(watch.WalkInterval() == 2000);
        std::cout << "  [PASS] walk throttle and back-off\n";
    }

    // Latched, then held (sat down to eat, started a cast, rooted): the
    // rescue stays latched but does not act until the bot is free again.
    {
        DungeonFollowStallWatch watch;
        CHECK(StandStill(watch, 0, 2000, 0.0f, 0.0f, 40.0f));
        CHECK(!watch.Observe(2100, 0.0f, 0.0f, 40.0f, kRange, true));
        CHECK(!StandStill(watch, 2200, 12000, 0.0f, 0.0f, 40.0f, true));
        CHECK(watch.rescuing);
        CHECK(watch.Observe(12100, 0.0f, 0.0f, 40.0f, kRange, false));
        std::cout << "  [PASS] latched, then held: yields while held\n";
    }

    // Five walks in a row that cannot start end the rescue for 60 s (a bot
    // below its leader's platform); after that the watch latches afresh.
    {
        DungeonFollowStallWatch watch;
        CHECK(StandStill(watch, 0, 2000, 0.0f, 0.0f, 40.0f));
        uint32_t t = 2000;
        for (uint32_t i = 0; i < 4; ++i)
        {
            CHECK(watch.WalkDue(t + watch.WalkInterval()));
            t += watch.WalkInterval();
            watch.Walked(t, false);
            CHECK(watch.rescuing);
        }
        t += watch.WalkInterval();
        CHECK(watch.WalkDue(t));
        watch.Walked(t, false);
        CHECK(!watch.rescuing);
        CHECK(!watch.WalkDue(t));
        uint32_t const gaveUp = t;
        CHECK(!StandStill(watch, gaveUp + 100, gaveUp + 59900, 0.0f, 0.0f, 40.0f));
        CHECK(!watch.WalkDue(gaveUp + 59900));
        CHECK(!watch.Observe(gaveUp + 60000, 0.0f, 0.0f, 40.0f, kRange, false));
        CHECK(!StandStill(watch, gaveUp + 60100, gaveUp + 61900, 0.0f, 0.0f, 40.0f));
        CHECK(StandStill(watch, gaveUp + 62000, gaveUp + 62000, 0.0f, 0.0f, 40.0f));
        std::cout << "  [PASS] five failed walks: give up for 60 s, then watch afresh\n";
    }

    // A started walk in between resets the failure count.
    {
        DungeonFollowStallWatch watch;
        CHECK(StandStill(watch, 0, 2000, 0.0f, 0.0f, 40.0f));
        for (uint32_t i = 0; i < 4; ++i)
            watch.Walked(3000 + i * 10000, false);
        watch.Walked(50000, true);
        for (uint32_t i = 0; i < 4; ++i)
            watch.Walked(60000 + i * 10000, false);
        CHECK(watch.rescuing);
        std::cout << "  [PASS] a started walk resets the failure count\n";
    }

    // Catching up during the give-up clears it at once.
    {
        DungeonFollowStallWatch watch;
        CHECK(StandStill(watch, 0, 2000, 0.0f, 0.0f, 40.0f));
        for (uint32_t i = 0; i < 5; ++i)
            watch.Walked(3000 + i * 10000, false);
        CHECK(watch.givenUp);
        CHECK(!watch.Observe(50000, 0.0f, 0.0f, 6.0f, kRange, false));
        CHECK(!watch.givenUp);
        std::cout << "  [PASS] caught up: give-up cleared\n";
    }

    // Timer wrap: the millisecond clock wraps without a false stall or a
    // stuck throttle.
    {
        DungeonFollowStallWatch watch;
        uint32_t const start = 0xFFFFFFFFu - 1000u;
        CHECK(!watch.Observe(start, 0.0f, 0.0f, 30.0f, kRange, false));
        bool rescuing = false;
        for (uint32_t i = 1; i <= 20; ++i)
            rescuing = watch.Observe(start + i * 100u, 0.0f, 0.0f, 30.0f, kRange, false);
        CHECK(rescuing);
        uint32_t const now = start + 2000u;
        watch.Walked(now, true);
        CHECK(!watch.WalkDue(now + 1999u));
        CHECK(watch.WalkDue(now + 2000u));
        std::cout << "  [PASS] timer wrap\n";
    }

    std::cout << "All dungeon follow stall policy tests passed.\n";
    return 0;
}
