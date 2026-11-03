// Standalone check for runtime/MemberParticipationPolicy.h (the run's participation clock): its forgetting is by
// last-seen time, never by last progress, and a shot that fired is progress while the auto-repeat spell being on and
// a self-cast are not.
//   g++ -std=c++17 -Wall -Wextra tools/test_member_participation_policy.cpp -o /tmp/test_member_participation_policy
#include "../runtime/MemberParticipationPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;

// Seventy tracked bots (past the old 64 bound) all seen this moment; most have not progressed at all. The clock
// keeps every one of them, the wanding bot's participation included: the old prune by actMs erased them.
static void TestSeventySeenBotsKeepTheirClocks()
{
    std::uint32_t const now = 500000;
    std::unordered_map<std::uint64_t, ParticipationClock> clocks;
    for (std::uint64_t guid = 1; guid <= 70; ++guid)
        clocks[guid] = ParticipationClock{0, now};
    clocks[70].actMs = now; // the wanding bot: a shot fired (the glue set actMs from the shot count)
    PruneParticipationClocks(clocks, now);
    CHECK(clocks.size() == 70);
    CHECK(clocks.count(70) == 1 && clocks[70].actMs == now);
}

// Unseen longer than a minute: forgotten once the map outgrows its bound; a bot seen this moment is kept whatever
// its progress.
static void TestStaleBotsAreForgottenByLastSeen()
{
    std::uint32_t const now = 500000;
    std::unordered_map<std::uint64_t, ParticipationClock> clocks;
    for (std::uint64_t guid = 1; guid <= 300; ++guid)
        clocks[guid] = ParticipationClock{0, guid <= 44 ? now - kParticipationForgetMs - 1000 : now};
    PruneParticipationClocks(clocks, now);
    for (std::uint64_t guid = 1; guid <= 44; ++guid)
        CHECK(clocks.count(guid) == 0);
    CHECK(clocks.size() == 256); // 256 fresh entries left, the bound
    CHECK(clocks.count(300) == 1);
}

// Over the bound with nothing stale: the least recently seen go, the map never grows past its bound.
static void TestTheBoundDropsTheLeastRecentlySeen()
{
    std::uint32_t const now = 500000;
    std::unordered_map<std::uint64_t, ParticipationClock> clocks;
    for (std::uint64_t guid = 1; guid <= 300; ++guid)
        clocks[guid] = ParticipationClock{0, now - static_cast<std::uint32_t>(300 - guid)};
    PruneParticipationClocks(clocks, now);
    CHECK(clocks.size() == 256);
    CHECK(clocks.count(1) == 0);  // the oldest seen
    CHECK(clocks.count(300) == 1);
}

// One progress reading: a running cast, a changed cast or a landed swing (a ranged one is a shot that fired) is
// progress; a self-cast is not, and neither is anything out of a fight.
static void TestProgressReadings()
{
    CHECK(ParticipationProgress(true, 133, true, 133, false, false));   // a cast still running
    CHECK(ParticipationProgress(true, 0, true, 133, false, false));     // the cast changed (finished)
    CHECK(ParticipationProgress(true, 0, true, 0, true, false));        // a melee swing
    CHECK(ParticipationProgress(true, 0, true, 0, false, true));        // a ranged swing (a shot fired)
    CHECK(!ParticipationProgress(true, 0, true, 0, false, false));      // nothing at all
    CHECK(!ParticipationProgress(false, 0, true, 0, true, true));       // out of the fight
    CHECK(!ParticipationProgress(true, 0, false, 0, false, false));     // the first read, no progress yet
    // A self-cast reads as no spell whatever else: the bot buffing itself where it stands is no progress.
    CHECK(ProgressSpell(133, true) == 0);
    CHECK(ProgressSpell(133, false) == 133);
    CHECK(!ParticipationProgress(true, ProgressSpell(133, true), true, 0, false, false));
}

// Auto Shot or a wand on with no shot fired: the auto-repeat flag alone is no progress, so the clock stays stale and
// the wedged rescue is due for it (MemberParticipates reads actedMs >= kParticipateMs). A shot that fired is the
// shot count moving, and that is progress.
static void TestAutoRepeatWithNoShotIsNoProgress()
{
    // 30 s of ticks with Auto Shot on, the bot out of reach and not shooting: no cast, no shot counted.
    // The clock's actMs stays where it was while seenMs moves on, so the member reads as not participating and the
    // wedged rescue is due for it (kParticipateMs).
    std::uint32_t actMs = 1000;
    std::uint32_t seenMs = 1000;
    for (std::uint32_t t = 2000; t <= 31000; t += 1000)
    {
        seenMs = t;
        if (ParticipationProgress(true, 0, true, 0, false, false))
            actMs = t;
    }
    CHECK(actMs == 1000);
    CHECK(seenMs - actMs >= 5000);
    // The first shot that fires (the shot count moves) is progress.
    CHECK(ParticipationProgress(true, 0, true, 0, false, true));
}

int main()
{
    TestSeventySeenBotsKeepTheirClocks();
    TestStaleBotsAreForgottenByLastSeen();
    TestTheBoundDropsTheLeastRecentlySeen();
    TestProgressReadings();
    TestAutoRepeatWithNoShotIsNoProgress();
    std::cout << "test_member_participation_policy: ok\n";
    return 0;
}
