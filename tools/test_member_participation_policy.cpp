// Standalone check for runtime/MemberParticipationPolicy.h (the run's participation clock): its forgetting is by
// last-seen time, never by last progress, and auto-repeat (Auto Shot, a wand) is progress while a self-cast is not.
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
    clocks[70].actMs = now; // the wanding bot: auto-repeat was its progress (the glue set actMs)
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

// One progress reading: a running cast, auto-repeat, a changed cast or a landed swing is progress; a self-cast is
// not, and neither is anything out of a fight.
static void TestProgressReadings()
{
    CHECK(ParticipationProgress(true, 133, 0, true, 133, false, false));   // a cast still running
    CHECK(ParticipationProgress(true, 0, 5019, true, 0, false, false));    // Auto Shot or a wand
    CHECK(ParticipationProgress(true, 0, 0, true, 133, false, false));     // the cast changed (finished)
    CHECK(ParticipationProgress(true, 0, 0, true, 0, true, false));        // a melee swing
    CHECK(ParticipationProgress(true, 0, 0, true, 0, false, true));        // a ranged swing
    CHECK(!ParticipationProgress(true, 0, 0, true, 0, false, false));      // nothing at all
    CHECK(!ParticipationProgress(false, 0, 5019, true, 0, true, true));    // out of the fight
    CHECK(!ParticipationProgress(true, 0, 0, false, 0, false, false));     // the first read, no progress yet
    // A self-cast reads as no spell whatever else: the bot buffing itself where it stands is no progress.
    CHECK(ProgressSpell(133, true) == 0);
    CHECK(ProgressSpell(133, false) == 133);
    CHECK(!ParticipationProgress(true, ProgressSpell(133, true), 0, true, 0, false, false));
}

int main()
{
    TestSeventySeenBotsKeepTheirClocks();
    TestStaleBotsAreForgottenByLastSeen();
    TestTheBoundDropsTheLeastRecentlySeen();
    TestProgressReadings();
    std::cout << "test_member_participation_policy: ok\n";
    return 0;
}
