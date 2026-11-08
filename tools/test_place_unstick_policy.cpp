// Standalone check for ai/playerbot/PlaceUnstickPolicy.h (navmesh-island unstick, AiPlayerbot.DungeonRunPlaceUnstick):
//   g++ -std=c++17 -Wall -Wextra tools/test_place_unstick_policy.cpp -o /tmp/test_place_unstick_policy && /tmp/test_place_unstick_policy
#include "../ai/playerbot/PlaceUnstickPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static constexpr uint32_t kShort = kPlaceUnstickFailDispatchShort;
static constexpr uint32_t kNoPath = kPlaceUnstickPathNoPath;

// Feed a failure every 2 s (the walk 55 trace cadence) from t0 for `secs` seconds.
static void FeedFails(PlaceUnstickState& s, uint32_t t0, uint32_t secs, uint32_t reason, uint32_t pathType)
{
    for (uint32_t t = 0; t <= secs * 1000; t += 2000)
        PlaceUnstickOnMoveResult(s, t0 + t, false, reason, pathType);
}

// The island priest of walk 55: in a raid, master 60 yd away, alive, free, not casting, no fight nearby.
static PlaceUnstickInputs IslandPriest(const PlaceUnstickState& s, uint32_t nowMs)
{
    PlaceUnstickInputs in;
    in.flagOn = true;
    in.inInstance = true;
    in.inBattleground = false;
    in.hasMaster = true;
    in.masterYd = 60.0f;
    in.botAlive = true;
    in.botCharmed = false;
    in.botCasting = false;
    in.masterFightNearBot = false;
    in.streakMs = PlaceUnstickStreakMs(s, nowMs);
    in.teleportedBefore = s.teleported;
    in.sinceTeleportMs = nowMs - s.lastTeleportMs;
    return in;
}

static void SeatOnePointTwoPriestOnIslandTeleportsToMaster()
{
    // seat 1.2 priest on a navmesh island: reach spell refused NOPATH 10 s, master 60 yd away -> teleport to master
    PlaceUnstickState s;
    uint32_t const t0 = 100000;
    FeedFails(s, t0, 10, kShort, kNoPath);
    uint32_t const now = t0 + 10000;
    CHECK(PlaceUnstickStreakMs(s, now) == 10000);
    CHECK(PlaceUnstickDecide(IslandPriest(s, now)) == PlaceUnstickVerdict::Teleport);
}

static void NineSecondsIsNotEnough()
{
    PlaceUnstickState s;
    uint32_t const t0 = 100000;
    for (uint32_t t = 0; t <= 9000; t += 1000)
        PlaceUnstickOnMoveResult(s, t0 + t, false, kShort, kNoPath);
    CHECK(PlaceUnstickStreakMs(s, t0 + 9000) == 9000);
    CHECK(PlaceUnstickDecide(IslandPriest(s, t0 + 9000)) == PlaceUnstickVerdict::StreakShort);
}

static void MasterWithinTwentyYardsIsNotCutOff()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.masterYd = 20.0f;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::MasterNear);
    in.masterYd = 12.0f;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::MasterNear);
    in.masterYd = 20.5f;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Teleport);
}

static void NeverInBattleground()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.inBattleground = true;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Battleground);
    // A battleground map is an instance too: the battleground test must win.
    in.inInstance = true;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Battleground);
}

static void NeverOutsideAnInstance()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.inInstance = false;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::NotInstance);
}

static void RateLimitOncePerThirtySeconds()
{
    PlaceUnstickState s;
    uint32_t const t0 = 100000;
    FeedFails(s, t0, 10, kShort, kNoPath);
    CHECK(PlaceUnstickDecide(IslandPriest(s, t0 + 10000)) == PlaceUnstickVerdict::Teleport);
    PlaceUnstickStamp(s, t0 + 10000);
    CHECK(s.lastStreakMs == 10000);
    CHECK(PlaceUnstickStreakMs(s, t0 + 10000) == 0);

    // Still stuck (the teleport was refused or landed on the island again): a fresh
    // 10 s streak is ready at +22 s, but the cooldown holds until +40 s.
    uint32_t const t1 = t0 + 12000;
    FeedFails(s, t1, 20, kShort, kNoPath);
    CHECK(PlaceUnstickStreakMs(s, t1 + 20000) == 20000);
    CHECK(PlaceUnstickDecide(IslandPriest(s, t1 + 20000)) == PlaceUnstickVerdict::Cooldown);
    for (uint32_t t = t1 + 22000; t <= t0 + 38000; t += 2000)
        PlaceUnstickOnMoveResult(s, t, false, kShort, kNoPath);
    CHECK(PlaceUnstickDecide(IslandPriest(s, t0 + 38000)) == PlaceUnstickVerdict::Cooldown);
    PlaceUnstickOnMoveResult(s, t0 + 40000, false, kShort, kNoPath);
    CHECK(PlaceUnstickDecide(IslandPriest(s, t0 + 40000)) == PlaceUnstickVerdict::Teleport);
}

static void FlagOffNeverTeleports()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 30, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 130000);
    in.flagOn = false;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::FlagOff);
}

static void OtherMoveFailureDoesNotCount()
{
    // dispatch-short without NOPATH (a clipped real path), and NOPATH under a different
    // failure site (hazard-short), must not start the clock.
    PlaceUnstickState s;
    FeedFails(s, 100000, 30, kShort, 0x0004 /* INCOMPLETE */);
    FeedFails(s, 100000, 30, kShort, 0x0001 /* NORMAL */);
    FeedFails(s, 100000, 30, 10 /* MOVE_FAIL_HAZARD_SHORT */, kNoPath);
    FeedFails(s, 100000, 30, 3 /* MOVE_FAIL_ARRIVED */, 0);
    CHECK(!s.streak);
    CHECK(PlaceUnstickStreakMs(s, 130000) == 0);
    CHECK(PlaceUnstickDecide(IslandPriest(s, 130000)) == PlaceUnstickVerdict::StreakShort);
    CHECK(PlaceUnstickCountsFail(kShort, kNoPath));
    CHECK(PlaceUnstickCountsFail(kShort, kNoPath | 0x0004));
    CHECK(!PlaceUnstickCountsFail(kShort, 0x0004));
    CHECK(!PlaceUnstickCountsFail(10, kNoPath));

    // Mixed in with counted failures they neither count nor reset the streak.
    PlaceUnstickState m;
    for (uint32_t t = 0; t <= 10000; t += 1000)
        PlaceUnstickOnMoveResult(m, 200000 + t, false, (t / 1000) % 2 ? kShort : 3u, (t / 1000) % 2 ? kNoPath : 0u);
    // counted fails at 1,3,5,7,9 s: streak 1 s .. 9 s = 8 s
    CHECK(PlaceUnstickStreakMs(m, 209000) == 8000);
}

static void LaunchedMoveResetsTheClock()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 8, kShort, kNoPath);
    PlaceUnstickOnMoveResult(s, 109000, true, 0, 0x0001);
    CHECK(!s.streak);
    FeedFails(s, 110000, 6, kShort, kNoPath);
    CHECK(PlaceUnstickDecide(IslandPriest(s, 116000)) == PlaceUnstickVerdict::StreakShort);
}

static void GapStartsANewStreak()
{
    PlaceUnstickState s;
    PlaceUnstickOnMoveResult(s, 100000, false, kShort, kNoPath);
    // 60 s with no move asked, then one fresh failure: not a 60 s streak.
    PlaceUnstickOnMoveResult(s, 160000, false, kShort, kNoPath);
    CHECK(PlaceUnstickStreakMs(s, 160000) == 0);
    // A lapsed streak reads 0 even before the next failure.
    PlaceUnstickState l;
    FeedFails(l, 100000, 12, kShort, kNoPath);
    CHECK(PlaceUnstickStreakMs(l, 112000 + kPlaceUnstickGapMs + 1) == 0);
}

static void BotStateGuards()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.botAlive = false;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::BotDead);
    in = IslandPriest(s, 112000);
    in.botCharmed = true;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Charmed);
    in = IslandPriest(s, 112000);
    in.botCasting = true;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Casting);
    in = IslandPriest(s, 112000);
    in.hasMaster = false;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::NoMaster);
}

static void FightNearTheBotIsNotASnag()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.masterFightNearBot = PlaceUnstickIsFight(true, true, 4.0f);
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Fight);
    CHECK(PlaceUnstickIsFight(true, true, 5.0f));
    CHECK(!PlaceUnstickIsFight(true, true, 5.5f));   // target farther than 5 yd: still a snag
    CHECK(!PlaceUnstickIsFight(false, true, 2.0f));  // master out of combat
    CHECK(!PlaceUnstickIsFight(true, false, 2.0f));  // no live target
}

static void WorldTimerWrap()
{
    PlaceUnstickState s;
    uint32_t const t0 = 0xFFFFFFFFu - 4000u;
    FeedFails(s, t0, 10, kShort, kNoPath);
    CHECK(PlaceUnstickStreakMs(s, t0 + 10000u) == 10000);
    CHECK(PlaceUnstickDecide(IslandPriest(s, t0 + 10000u)) == PlaceUnstickVerdict::Teleport);
}

int main()
{
    SeatOnePointTwoPriestOnIslandTeleportsToMaster();
    NineSecondsIsNotEnough();
    MasterWithinTwentyYardsIsNotCutOff();
    NeverInBattleground();
    NeverOutsideAnInstance();
    RateLimitOncePerThirtySeconds();
    FlagOffNeverTeleports();
    OtherMoveFailureDoesNotCount();
    LaunchedMoveResetsTheClock();
    GapStartsANewStreak();
    BotStateGuards();
    FightNearTheBotIsNotASnag();
    WorldTimerWrap();
    std::cout << "test_place_unstick_policy: all checks passed\n";
    return 0;
}
