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

// The walk 55 island spot in Zul'Gurub (map 309).
static PlaceUnstickSpot Zg(float dx = 0.0f)
{
    PlaceUnstickSpot at;
    at.inInstance = true;
    at.mapId = 309;
    at.instanceId = 7;
    at.x = -11562.7f + dx;
    at.y = -1316.0f;
    at.z = 78.6f;
    return at;
}

// Feed a failure every 2 s (the walk 55 trace cadence) from t0 for `secs` seconds.
static void FeedFails(PlaceUnstickState& s, uint32_t t0, uint32_t secs, uint32_t reason, uint32_t pathType,
    const PlaceUnstickSpot& at = Zg())
{
    for (uint32_t t = 0; t <= secs * 1000; t += 2000)
        PlaceUnstickOnMoveResult(s, t0 + t, false, reason, pathType, at);
}

// The island priest of walk 55: in a raid, master 60 yd away and in sight, alive, free, not casting,
// no fight nearby, and the isolation query proved the start cut off.
static PlaceUnstickInputs IslandPriest(const PlaceUnstickState& s, uint32_t nowMs)
{
    PlaceUnstickInputs in;
    in.flagOn = true;
    in.inInstance = true;
    in.inBattleground = false;
    in.hasMaster = true;
    in.masterYd = 60.0f;
    in.masterInSight = true;
    in.runCommanded = false;
    in.botAlive = true;
    in.botCharmed = false;
    in.botCharming = false;
    in.botCasting = false;
    in.masterFightNearBot = false;
    in.streakMs = PlaceUnstickStreakMs(s, nowMs);
    in.teleportedBefore = s.teleported;
    in.sinceTeleportMs = nowMs - s.lastTeleportMs;
    // The walk 55 query: start poly 0.4 yd away, master's poly found, the path stops at the start.
    in.startIsolated = PlaceUnstickStartIsolated(true, true, true, false, 0.3f);
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

static void DestinationOffMeshBotOnMainMeshNoTeleport()
{
    // destination off-mesh, bot on the main mesh -> no teleport
    // The core stamps NOPATH for an off-mesh END poly too (a mob on a ledge, a master on a crate): the
    // move clock runs exactly like the island's, so only the isolation query tells them apart.
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.startIsolated = PlaceUnstickStartIsolated(true, true, false, false, 0.0f); // no poly under the master
    CHECK(!in.startIsolated);
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::NotIsolated);
    // On the main mesh with a partial path that gets 40 yd toward the master: connected.
    in.startIsolated = PlaceUnstickStartIsolated(true, true, true, false, 40.0f);
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::NotIsolated);
    // A complete path: connected.
    in.startIsolated = PlaceUnstickStartIsolated(true, true, true, true, 0.0f);
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::NotIsolated);
    // No navmesh answer: nothing proven.
    CHECK(!PlaceUnstickStartIsolated(false, false, false, false, 0.0f));
    // No poly under the bot at all: cut off.
    CHECK(PlaceUnstickStartIsolated(true, false, false, false, 0.0f));
    // The reach bound: 2 yd in, 2.1 yd out.
    CHECK(PlaceUnstickStartIsolated(true, true, true, false, 2.0f));
    CHECK(!PlaceUnstickStartIsolated(true, true, true, false, 2.1f));
}

static void NotIsolatedRestartsTheStreak()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 10, kShort, kNoPath);
    PlaceUnstickRestartStreak(s, 110000);
    CHECK(PlaceUnstickStreakMs(s, 110000) == 0);
    FeedFails(s, 112000, 8, kShort, kNoPath);
    CHECK(PlaceUnstickDecide(IslandPriest(s, 120000)) == PlaceUnstickVerdict::Teleport);
}

static void NineSecondsIsNotEnough()
{
    PlaceUnstickState s;
    uint32_t const t0 = 100000;
    for (uint32_t t = 0; t <= 9000; t += 1000)
        PlaceUnstickOnMoveResult(s, t0 + t, false, kShort, kNoPath, Zg());
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

static void MasterBeyondSightOnlyForACommandedGroup()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.masterYd = 300.0f;
    in.masterInSight = false;
    in.runCommanded = false;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::MasterTooFar);
    in.runCommanded = true;   // a dungeon run or raid walk commander binds the group
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Teleport);
    in.runCommanded = false;
    in.masterInSight = true;  // within sightDistance, no commander
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Teleport);
}

static void NeverInBattleground()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 12, kShort, kNoPath);
    PlaceUnstickInputs in = IslandPriest(s, 112000);
    in.inBattleground = true;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Battleground);
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

static void OpenWorldNineSecondsThenOneDungeonFailNoTeleport()
{
    // 9 s in the open world, then 1 failure in a dungeon -> no teleport
    PlaceUnstickState s;
    PlaceUnstickSpot world;
    world.inInstance = false;
    world.mapId = 0;
    world.x = -9000.0f;
    for (uint32_t t = 0; t <= 9000; t += 1000)
        PlaceUnstickOnMoveResult(s, 100000 + t, false, kShort, kNoPath, world);
    CHECK(!s.streak);
    PlaceUnstickOnMoveResult(s, 110000, false, kShort, kNoPath, Zg());
    CHECK(PlaceUnstickStreakMs(s, 110000) == 0);
    CHECK(PlaceUnstickDecide(IslandPriest(s, 110000)) == PlaceUnstickVerdict::StreakShort);

    // A new instance id (same map) restarts the streak too.
    PlaceUnstickState m;
    FeedFails(m, 100000, 8, kShort, kNoPath);
    PlaceUnstickSpot other = Zg();
    other.instanceId = 8;
    PlaceUnstickOnMoveResult(m, 110000, false, kShort, kNoPath, other);
    CHECK(PlaceUnstickStreakMs(m, 110000) == 0);
}

static void DriftOfThreeYardsRestartsTheStreak()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 8, kShort, kNoPath);
    // 2.9 yd from the streak's start: same streak.
    PlaceUnstickOnMoveResult(s, 110000, false, kShort, kNoPath, Zg(2.9f));
    CHECK(PlaceUnstickStreakMs(s, 110000) == 10000);
    CHECK(PlaceUnstickDecide(IslandPriest(s, 110000)) == PlaceUnstickVerdict::Teleport);
    // 3 yd: it moved, the streak starts again from there.
    PlaceUnstickState d;
    FeedFails(d, 100000, 8, kShort, kNoPath);
    PlaceUnstickOnMoveResult(d, 110000, false, kShort, kNoPath, Zg(3.0f));
    CHECK(PlaceUnstickStreakMs(d, 110000) == 0);
    CHECK(PlaceUnstickDecide(IslandPriest(d, 110000)) == PlaceUnstickVerdict::StreakShort);
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
        PlaceUnstickOnMoveResult(s, t, false, kShort, kNoPath, Zg());
    CHECK(PlaceUnstickDecide(IslandPriest(s, t0 + 38000)) == PlaceUnstickVerdict::Cooldown);
    PlaceUnstickOnMoveResult(s, t0 + 40000, false, kShort, kNoPath, Zg());
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
        PlaceUnstickOnMoveResult(m, 200000 + t, false, (t / 1000) % 2 ? kShort : 3u, (t / 1000) % 2 ? kNoPath : 0u, Zg());
    // counted fails at 1,3,5,7,9 s: streak 1 s .. 9 s = 8 s
    CHECK(PlaceUnstickStreakMs(m, 209000) == 8000);
}

static void LaunchedMoveResetsTheClock()
{
    PlaceUnstickState s;
    FeedFails(s, 100000, 8, kShort, kNoPath);
    PlaceUnstickOnMoveResult(s, 109000, true, 0, 0x0001, Zg());
    CHECK(!s.streak);
    FeedFails(s, 110000, 6, kShort, kNoPath);
    CHECK(PlaceUnstickDecide(IslandPriest(s, 116000)) == PlaceUnstickVerdict::StreakShort);
}

static void GapStartsANewStreak()
{
    PlaceUnstickState s;
    PlaceUnstickOnMoveResult(s, 100000, false, kShort, kNoPath, Zg());
    PlaceUnstickOnMoveResult(s, 160000, false, kShort, kNoPath, Zg());
    CHECK(PlaceUnstickStreakMs(s, 160000) == 0);
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
    in.botCharming = true;
    CHECK(PlaceUnstickDecide(in) == PlaceUnstickVerdict::Charming);
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
    CHECK(!PlaceUnstickIsFight(true, true, 5.5f));
    CHECK(!PlaceUnstickIsFight(false, true, 2.0f));
    CHECK(!PlaceUnstickIsFight(true, false, 2.0f));
}

static void StayGuardAndPet()
{
    // A stay/guard point on the island (within 5 yd of its start) is cleared; one elsewhere is kept.
    CHECK(PlaceUnstickClearsAnchor(true, true, 4.9f));
    CHECK(PlaceUnstickClearsAnchor(true, true, 5.0f));
    CHECK(!PlaceUnstickClearsAnchor(true, true, 5.1f));
    CHECK(!PlaceUnstickClearsAnchor(false, true, 1.0f));
    CHECK(!PlaceUnstickClearsAnchor(true, false, 1.0f));
    // The pet goes along within 30 yd of the island start.
    CHECK(PlaceUnstickMovesPet(true, 30.0f));
    CHECK(!PlaceUnstickMovesPet(true, 30.5f));
    CHECK(!PlaceUnstickMovesPet(false, 2.0f));
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
    DestinationOffMeshBotOnMainMeshNoTeleport();
    NotIsolatedRestartsTheStreak();
    NineSecondsIsNotEnough();
    MasterWithinTwentyYardsIsNotCutOff();
    MasterBeyondSightOnlyForACommandedGroup();
    NeverInBattleground();
    NeverOutsideAnInstance();
    OpenWorldNineSecondsThenOneDungeonFailNoTeleport();
    DriftOfThreeYardsRestartsTheStreak();
    RateLimitOncePerThirtySeconds();
    FlagOffNeverTeleports();
    OtherMoveFailureDoesNotCount();
    LaunchedMoveResetsTheClock();
    GapStartsANewStreak();
    BotStateGuards();
    FightNearTheBotIsNotASnag();
    StayGuardAndPet();
    WorldTimerWrap();
    std::cout << "test_place_unstick_policy: all checks passed\n";
    return 0;
}
