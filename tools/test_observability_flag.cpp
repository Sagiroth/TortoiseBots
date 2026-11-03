// Standalone tests for the two-file gate on a dashboard feed (spec dash-raids-tab): AiPlayerbot.ObservabilityRaids is set
// in aiplayerbot.conf and in the world config, the feed is on only while both allow it, and a `.reload config` that turns
// either file to 0 closes the books the sampler fed (the off-turn path of tools/test_observed_raid_policy.cpp).
//
// The headers include the standard library only, so this file compiles them without a game header.
//
// Build and run:
//   g++ -std=c++20 -Wall -Wextra tools/test_observability_flag.cpp -o /tmp/test_observability_flag
//   /tmp/test_observability_flag

#include "../runtime/ObservabilityFlag.h"
#include "../runtime/ObservedRaidPolicy.h"

#include <cstdio>
#include <utility>
#include <vector>

using TortoiseBots::ObservabilityFlag::Feed;
using namespace TortoiseBots::ObservedRaid;

namespace {

int g_failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// The sampler's books while the feed feeds them: the groups it holds, and the close/forget calls the off turn makes.
// CloseUnfinished ends an open fight at the turn's time.
struct Books
{
    std::vector<uint32_t> held = { 101, 202 };
    std::vector<std::pair<uint32_t, uint32_t>> closed;
    std::vector<uint32_t> forgotten;

    void ReleaseOnTurn(FlagTurn& turn, bool enabled, uint32_t nowMs)
    {
        if (!turn.TurnedOff(enabled))
            return;
        std::vector<uint32_t> const groups = held;
        ReleaseBooks(groups, nowMs,
            [&](uint32_t groupId, uint32_t atMs) { closed.emplace_back(groupId, atMs); },
            [&](uint32_t groupId) { forgotten.push_back(groupId); });
        held.clear();
    }
};

} // namespace

// T-F1: the feed is on only while both config files allow it; a 0 in either one, and only that one at 1, switches it off.
void TestBothFilesMustAllow()
{
    Feed feed;
    Check(!feed.Read(false, false), "T-F1 neither file at 1: off");
    Check(!feed.Read(false, true), "T-F1 the world file alone: off");
    Check(!feed.Read(true, false), "T-F1 aiplayerbot.conf alone: off");
    Check(feed.Read(true, true), "T-F1 both files at 1: on");
    Check(!feed.Read(true, false), "T-F1 a 0 back in the world file: off");
    Check(feed.Read(true, true), "T-F1 both back at 1: on");
}

// T-F2: the feed on from both files; a `.reload config` that turns the named file to 0 takes the feed off, and the
// sampler's next tick closes every book it held at the turn's time and forgets it. Only one turn per off period.
void TestReloadTurnsOff(char const* what, bool aiFile, bool worldFile)
{
    Feed feed;
    FlagTurn turn;
    Books books;

    Check(feed.Read(true, true), "T-F2 both files at 1 start the feed on");
    Check(!turn.TurnedOff(feed.enabled), "T-F2 the on tick is no turn");

    // A reload that keeps both files at 1 changes nothing: no turn, the books stay fed.
    Check(feed.Read(true, true), "T-F2 a reload with both files still at 1 keeps the feed on");
    Check(!turn.TurnedOff(feed.enabled), "T-F2 a reload that keeps it on is no turn");
    Check(books.closed.empty() && books.held.size() == 2, "T-F2 nothing is released while the feed stays on");

    // The reload: one file now says 0.
    bool const enabled = feed.Read(aiFile, worldFile);
    Check(!enabled, what);
    books.ReleaseOnTurn(turn, enabled, 4242);
    Check(books.closed.size() == 2, "T-F2 the held books are closed on the turn");
    Check(books.forgotten.size() == 2, "T-F2 the held books are forgotten");
    if (books.closed.size() == 2)
    {
        Check(books.closed[0].first == 101 && books.closed[0].second == 4242,
              "T-F2 the open fight closes at the turn's time");
        Check(books.closed[1].first == 202, "T-F2 every held group is closed, not only the fighting one");
    }
    Check(books.held.empty(), "T-F2 the sampler drops its set on the turn");

    // The off tick after the turn is no second turn.
    books.ReleaseOnTurn(turn, enabled, 9000);
    Check(books.closed.size() == 2, "T-F2 no second release while off");

    // Back on, then off again: the new on tick starts from an empty set and the next off turn releases only what it fed.
    Check(feed.Read(true, true), "T-F2 both files at 1 again turn the feed back on");
    Check(!turn.TurnedOff(feed.enabled), "T-F2 turning back on is no turn");
    Check(!feed.Read(false, true), "T-F2 the AI file's 0 turns it off again");
    books.ReleaseOnTurn(turn, feed.enabled, 9100);
    Check(books.closed.size() == 2, "T-F2 an empty set releases nothing on the second turn");
}

int main()
{
    TestBothFilesMustAllow();
    TestReloadTurnsOff("T-F2 a 0 in aiplayerbot.conf takes the feed off", false, true);
    TestReloadTurnsOff("T-F3 a 0 in the world config takes the feed off", true, false);
    if (g_failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_observability_flag: all checks passed\n");
    return 0;
}
