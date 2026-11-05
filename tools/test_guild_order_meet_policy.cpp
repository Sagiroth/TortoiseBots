// Standalone test for runtime/GuildOrderMeetPolicy.h: the meet verdict
// order and the enchant replace rule. No core, no DB: feeds the policy plain
// facts.
#include "../runtime/GuildOrderMeetPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // Capitals split by faction.
    CHECK(EnchantCapitalZone(true) == 1519);
    CHECK(EnchantCapitalZone(false) == 1637);
    CHECK(GiverInBotCapital(1519, true));
    CHECK(!GiverInBotCapital(1637, true));
    CHECK(GiverInBotCapital(1637, false));
    CHECK(!GiverInBotCapital(1519, false));

    // Meet verdict, in check order: no order, capital, tied up, gathering.
    MeetFacts none;
    CHECK(DecideMeet(none) == MeetVerdict::NoOrder);
    MeetFacts away;
    away.liveEnchantOrder = true;
    away.giverInCapital = false;
    away.botTiedUp = true;
    away.gathering = true;
    CHECK(DecideMeet(away) == MeetVerdict::NotCapital);
    MeetFacts tied;
    tied.liveEnchantOrder = true;
    tied.giverInCapital = true;
    tied.botTiedUp = true;
    tied.gathering = true;
    CHECK(DecideMeet(tied) == MeetVerdict::Later);
    MeetFacts busy;
    busy.liveEnchantOrder = true;
    busy.giverInCapital = true;
    busy.gathering = true;
    CHECK(DecideMeet(busy) == MeetVerdict::Gathering);
    MeetFacts ready;
    ready.liveEnchantOrder = true;
    ready.giverInCapital = true;
    CHECK(DecideMeet(ready) == MeetVerdict::Coming);

    // Replace rule: only another giver's unfinished enchant order blocks,
    // and only for a giver who is neither master nor officer.
    CHECK(DecideEnchantReplace(false, false, false) == EnchantReplaceVerdict::Allow);
    CHECK(DecideEnchantReplace(true, true, false) == EnchantReplaceVerdict::Allow);
    CHECK(DecideEnchantReplace(true, false, true) == EnchantReplaceVerdict::Allow);
    CHECK(DecideEnchantReplace(true, false, false) == EnchantReplaceVerdict::Busy);

    if (failures == 0)
        std::printf("test_guild_order_meet_policy ok\n");
    return failures ? 1 : 0;
}
