// Standalone test for runtime/GuildOrderEnchantLifecyclePolicy.h: the meet,
// coming and trade-gate decisions behind the enchant lifecycle action.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_enchant_lifecycle_policy.cpp -o /tmp/test_guild_order_enchant_lifecycle_policy
//   /tmp/test_guild_order_enchant_lifecycle_policy
#include "../runtime/GuildOrderEnchantLifecyclePolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static EnchantLifecycleFacts Gathering()
{
    EnchantLifecycleFacts facts;
    facts.liveEnchantOrder = true;
    facts.gathering = true;
    facts.teleportOk = true;
    return facts;
}

int main()
{
    // Gather teleport: out of capital and free goes, in capital or tied up waits.
    CHECK(DecideEnchantGatherTeleport(Gathering()) == EnchantLifecycleStep::Teleport);
    EnchantLifecycleFacts inCapital = Gathering();
    inCapital.botInCapital = true;
    CHECK(DecideEnchantGatherTeleport(inCapital) == EnchantLifecycleStep::Idle);
    EnchantLifecycleFacts tied = Gathering();
    tied.botTiedUp = true;
    CHECK(DecideEnchantGatherTeleport(tied) == EnchantLifecycleStep::Idle);
    EnchantLifecycleFacts fighting = Gathering();
    fighting.teleportOk = false;
    CHECK(DecideEnchantGatherTeleport(fighting) == EnchantLifecycleStep::Idle);
    EnchantLifecycleFacts failed = Gathering();
    failed.teleportFailed = true;
    CHECK(DecideEnchantGatherTeleport(failed) == EnchantLifecycleStep::RetryTeleport);
    EnchantLifecycleFacts ready = Gathering();
    ready.gathering = false;
    CHECK(DecideEnchantGatherTeleport(ready) == EnchantLifecycleStep::Idle);

    // Meet: ready with a live request in the capital comes; gathering or a
    // gone giver waits.
    EnchantLifecycleFacts meet;
    meet.liveEnchantOrder = true;
    meet.meetPending = true;
    meet.giverOnline = true;
    meet.giverInCapital = true;
    CHECK(DecideEnchantMeet(meet) == EnchantLifecycleStep::ToComing);
    EnchantLifecycleFacts offline = meet;
    offline.giverOnline = false;
    CHECK(DecideEnchantMeet(offline) == EnchantLifecycleStep::Idle);
    EnchantLifecycleFacts away = meet;
    away.giverInCapital = false;
    CHECK(DecideEnchantMeet(away) == EnchantLifecycleStep::Idle);
    EnchantLifecycleFacts busy = meet;
    busy.botTiedUp = true;
    CHECK(DecideEnchantMeet(busy) == EnchantLifecycleStep::Idle);
    EnchantLifecycleFacts stillWorking = meet;
    stillWorking.gathering = true;
    CHECK(DecideEnchantMeet(stillWorking) == EnchantLifecycleStep::Idle);

    // Coming: a gone giver expires the meet, in range opens the trade.
    EnchantLifecycleFacts coming;
    coming.liveEnchantOrder = true;
    coming.meetPending = true;
    CHECK(DecideEnchantComing(coming) == EnchantLifecycleStep::Walk);
    EnchantLifecycleFacts close = coming;
    close.atTradeDistance = true;
    CHECK(DecideEnchantComing(close) == EnchantLifecycleStep::OpenTrade);
    EnchantLifecycleFacts left = coming;
    left.giverLeft = true;
    left.atTradeDistance = true;
    CHECK(DecideEnchantComing(left) == EnchantLifecycleStep::MeetExpired);

    // Trade gates.
    CHECK(EnchantWindowIsMaterialsTrade(true));
    CHECK(!EnchantWindowIsMaterialsTrade(false));
    CHECK(EnchantTradeWithStranger(true, false));
    CHECK(!EnchantTradeWithStranger(true, true));
    CHECK(!EnchantTradeWithStranger(false, false));
    CHECK(EnchantDoneVerified(123, 123));
    CHECK(!EnchantDoneVerified(0, 123));
    CHECK(!EnchantDoneVerified(124, 123));

    // Clock pause covers the meet states for enchant rows only.
    CHECK(EnchantStatePausesOrderClocks(true, 8, 9, 10, 8));
    CHECK(EnchantStatePausesOrderClocks(true, 8, 9, 10, 9));
    CHECK(EnchantStatePausesOrderClocks(true, 8, 9, 10, 10));
    CHECK(!EnchantStatePausesOrderClocks(true, 8, 9, 10, 0));
    CHECK(!EnchantStatePausesOrderClocks(false, 8, 9, 10, 9));

    // Restart downgrades Coming/Enchanting, keeps everything else.
    CHECK(EnchantRestartState(9, 10, 8, 7, false, 9) == 8);
    CHECK(EnchantRestartState(9, 10, 8, 7, true, 10) == 7);
    CHECK(EnchantRestartState(9, 10, 8, 7, false, 10) == 8);
    CHECK(EnchantRestartState(9, 10, 8, 7, false, 0) == 0);

    if (failures)
    {
        std::printf("enchant-lifecycle policy: %d FAILURES\n", failures);
        return 1;
    }
    std::printf("enchant-lifecycle policy: OK\n");
    return 0;
}
