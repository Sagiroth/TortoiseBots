// Standalone tests for the observed-raid sampler's rules (spec dash-raids-tab): the bot's copy a group is filed under,
// the selected-copy filter that keeps a member in another instance out of its group's fight, and the flag's off turn that
// closes and forgets the books the sampler fed (`.reload config` turning AiPlayerbot.ObservabilityRaids off).
//
// The header includes the standard library only, so this file compiles it without a game header.
//
// Build and run:
//   g++ -std=c++20 -Wall -Wextra tools/test_observed_raid_policy.cpp -o /tmp/test_observed_raid_policy
//   /tmp/test_observed_raid_policy

#include "../runtime/ObservedRaidPolicy.h"

#include <algorithm>
#include <cstdio>
#include <vector>

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

// One group's 1 Hz sample as UpdateObservedRaids hands it to the book: the online members, each on a map copy. The test
// asserts the feed (any member of the selected copy in combat, all of them dead, the first as the boss anchor) because
// that is what opens a fight, keeps the book's out-of-combat clock from running out, and picks the anchor.
struct Raid
{
    uint32_t map = 409;       // Molten Core, the group's selected copy
    uint32_t instance = 7;

    struct Unit
    {
        uint32_t map = 0;
        uint32_t instance = 0;
        bool inCombat = false;
        bool alive = true;
    };

    struct Feed
    {
        bool anyInCombat = false;
        bool allDead = false;
        int anchor = -1;   // index into the online list the boss pick reads, -1 when the copy holds nobody
        size_t fed = 0;    // the members the book is fed from
    };

    Feed Sample(std::vector<Unit> const& online) const
    {
        std::vector<size_t> onCopy;
        for (size_t i = 0; i < online.size(); ++i)
            if (OnSelectedCopy(map, instance, online[i].map, online[i].instance))
                onCopy.push_back(i);
        Feed feed;
        feed.allDead = !onCopy.empty();
        feed.fed = onCopy.size();
        for (size_t const i : onCopy)
        {
            feed.anyInCombat = feed.anyInCombat || online[i].inCombat;
            feed.allDead = feed.allDead && !online[i].alive;
        }
        if (!onCopy.empty())
            feed.anchor = static_cast<int>(onCopy.front());
        return feed;
    }
};

// The books a fake CombatMonitorService holds, so the off turn can be observed without the service.
struct Books
{
    struct Call
    {
        uint32_t groupId = 0;
        uint32_t atMs = 0;
    };

    std::vector<uint32_t> inFight;
    std::vector<Call> closed;
    std::vector<uint32_t> forgotten;

    bool Fighting(uint32_t groupId) const
    {
        for (uint32_t const id : inFight)
            if (id == groupId)
                return true;
        return false;
    }
};

} // namespace

// T-R1: a member in another instance copy, on another map or outdoors opens neither the fight nor the anchor game.
void TestSelectedCopy()
{
    Check(OnSelectedCopy(409, 7, 409, 7), "T-R1 the selected copy is fed");
    Check(!OnSelectedCopy(409, 7, 409, 8), "T-R1 another instance of the same map is not");
    Check(!OnSelectedCopy(409, 7, 47, 7), "T-R1 another map is not");
    Check(!OnSelectedCopy(409, 7, 0, 0), "T-R1 an outdoors unit is not");
    Check(!OnSelectedCopy(409, 0, 409, 0), "T-R1 a group without a selected copy feeds nothing");

    // The raid's own member opens the fight and is the anchor; a member that moved to another instance of the same map,
    // or out to the open world, is not in the feed at all.
    Raid raid;
    Raid::Feed const own = raid.Sample({ { 409, 7, true }, { 409, 8, true }, { 0, 0, true } });
    Check(own.anyInCombat, "T-R1 a fighting member of the copy opens the fight");
    Check(!own.allDead, "T-R1 its copy is not wiped");
    Check(own.anchor == 0, "T-R1 the anchor is the fed member of the copy");
    Check(own.fed == 1, "T-R1 one member of the two-instance group is fed");

    // The straggler: the copy's members left combat, the member in the other instance fights on. It neither opens nor
    // extends the fight (its combat is never read), and being alive elsewhere does not hide the copy's wipe either.
    Raid::Feed const straggler = raid.Sample({ { 409, 7, false }, { 409, 8, true }, { 0, 0, true } });
    Check(!straggler.anyInCombat, "T-R1 another instance's combat never extends the raid's fight");
    Check(!straggler.allDead, "T-R1 an on-copy member alive keeps the wipe from being read");

    // Only the other instances hold anyone: nothing to feed, nothing to anchor on, and the copy reads as no combat.
    Raid::Feed const elsewhere = raid.Sample({ { 409, 8, true }, { 409, 9, true } });
    Check(!elsewhere.anyInCombat, "T-R1 another instance never opens the raid's fight");
    Check(elsewhere.anchor == -1, "T-R1 no member of the copy: no anchor");
    Check(elsewhere.fed == 0, "T-R1 no member of the copy: an empty feed");

    // The wipe is the copy's: every fed member dead, even while a member elsewhere still lives.
    Raid::Feed const wipe = raid.Sample({ { 409, 7, true, false }, { 409, 8, false, true } });
    Check(wipe.allDead, "T-R1 allDead of the copy reads the wipe");

    // A single group split over two copies of the same map: only the selected one counts.
    Raid split;
    split.instance = 12;
    Check(!split.Sample({ { 409, 11, true }, { 409, 12, false } }).anyInCombat,
          "T-R1 the other copy of the map is not fed");
    Check(split.Sample({ { 409, 11, false }, { 409, 12, true } }).anyInCombat,
          "T-R1 the selected copy opens it");
}

// T-R3: the group is filed and sampled under a bot's copy, never a human's. A human standing in the raid copy cannot make
// the group a row when the group's only bot is elsewhere, and the sampler does not feed the copy the bot left.
void TestBotCopy()
{
    auto const member = [](bool leader, bool bot, bool qualifies, uint32_t map, uint32_t instance)
    {
        return CopyMember{ leader, bot, qualifies, map, instance };
    };

    // The sampler's read of a group: the online members on the selected copy, and whether one of them is a bot.
    struct Tick
    {
        size_t onCopy = 0;
        bool anyBot = false;
        bool sampled = false;
    };
    auto const tick = [](std::vector<CopyMember> const& online, CopyChoice const& copy)
    {
        Tick out;
        for (CopyMember const& m : online)
        {
            if (!OnSelectedCopy(copy.map, copy.instance, m.map, m.instance))
                continue;
            ++out.onCopy;
            out.anyBot = out.anyBot || m.bot;
        }
        out.sampled = SampleHoldsBot(copy.instance, out.anyBot);
        return out;
    };

    // The group's human leader stands in 409/7; the group's only bot is outdoors.
    std::vector<CopyMember> const humanCopy{ member(true, false, true, 409, 7), member(false, true, false, 0, 0) };
    CopyChoice const none = SelectCopy(humanCopy);
    Check(!none.found, "T-R3 a human's copy never emits a group whose only bot is elsewhere");
    Tick const empty = tick(humanCopy, none);
    Check(!empty.sampled && empty.onCopy == 0, "T-R3 the copy with no bot is not sampled");

    // A bot stands in that copy: the human leader does not change which copy names the group.
    std::vector<CopyMember> const botCopy{ member(true, false, true, 409, 7), member(false, true, true, 409, 7) };
    CopyChoice const chosen = SelectCopy(botCopy);
    Check(chosen.found && chosen.map == 409 && chosen.instance == 7, "T-R3 a bot in the copy emits the group under it");
    Tick const live = tick(botCopy, chosen);
    Check(live.sampled && live.anyBot && live.onCopy == 2, "T-R3 the copy with the bot is sampled");

    // The bot moved out between collections: the same row is no longer sampled, and the humans in the copy feed nothing.
    std::vector<CopyMember> const left{ member(true, false, true, 409, 7), member(false, true, false, 0, 0) };
    Tick const gone = tick(left, chosen);
    Check(gone.onCopy == 1 && !gone.sampled, "T-R3 the copy the bot left is not sampled");

    // A bot outdoors and a bot in another copy: only a qualifying bot names the group, and it is not the humans' copy.
    std::vector<CopyMember> const mixed{ member(true, false, true, 409, 7), member(false, true, false, 0, 0),
        member(false, true, true, 409, 9) };
    CopyChoice const other = SelectCopy(mixed);
    Check(other.found && other.instance == 9, "T-R3 a bot's own copy names the group, not the humans'");

    // Two bots in two copies: the leader's bot copy wins, else the first bot's.
    std::vector<CopyMember> const leaderBot{ member(true, true, true, 409, 8), member(false, true, true, 409, 7) };
    CopyChoice const lead = SelectCopy(leaderBot);
    Check(lead.found && lead.instance == 8, "T-R3 the leader's bot copy wins");
    std::vector<CopyMember> const twoBots{ member(true, false, true, 409, 7), member(false, true, true, 409, 7),
        member(false, true, true, 409, 9) };
    CopyChoice const first = SelectCopy(twoBots);
    Check(first.found && first.instance == 7, "T-R3 else the first bot's copy");

    // No bot at all: no copy, no row, nothing sampled.
    std::vector<CopyMember> const humans{ member(true, false, true, 409, 7), member(false, false, true, 409, 7) };
    Check(!SelectCopy(humans).found, "T-R3 a group of real players only is not emitted");
}

// T-R2: the enabled -> disabled turn closes each held fight at the current time, forgets its book and drops the sampler's
// state; a bound group's book is the bound path's and is left alone.
void TestOffTurn()
{
    FlagTurn turn;
    Check(!turn.TurnedOff(true), "T-R2 an on tick after no on tick is no turn");
    Check(!turn.TurnedOff(true), "T-R2 an on tick stays on");
    Check(turn.TurnedOff(false), "T-R2 the on -> off tick is the turn");
    Check(!turn.TurnedOff(false), "T-R2 the off tick after it is no turn again");
    Check(!turn.TurnedOff(true), "T-R2 turning back on is no turn");

    // The sampler's state: a group it collected (an open fight, a binding that was released so its book stayed observed)
    // and a group on its gone-grace clock, plus a group that is bound now.
    std::vector<uint32_t> const collected{ 101, 303 };
    std::vector<uint32_t> const away{ 202, 101 };
    auto const bound = [](uint32_t groupId) { return groupId == 303; };
    std::vector<uint32_t> const held = HeldGroups(collected, away, bound);
    Check(held.size() == 2, "T-R2 each held group once, the bound one left out");
    if (held.size() == 2)
        Check(held[0] == 101 && held[1] == 202, "T-R2 collected first, then the gone-grace clock");

    Books books;
    books.inFight = { 101, 303 };
    std::vector<uint32_t> live = collected;
    std::vector<uint32_t> retired = away;
    if (turn.TurnedOff(false))
    {
        ReleaseBooks(HeldGroups(live, retired, bound), 4242,
            [&](uint32_t groupId, uint32_t atMs)
            {
                books.closed.push_back(Books::Call{ groupId, atMs });
                books.inFight.erase(std::remove(books.inFight.begin(), books.inFight.end(), groupId),
                    books.inFight.end());
            },
            [&](uint32_t groupId) { books.forgotten.push_back(groupId); });
        live.clear();
        retired.clear();
    }
    Check(books.closed.size() == 2, "T-R2 both held books are closed on the turn");
    if (books.closed.size() == 2)
    {
        Check(books.closed[0].groupId == 101 && books.closed[0].atMs == 4242,
              "T-R2 the open fight is closed at the turn's time");
        Check(books.closed[1].groupId == 202, "T-R2 the gone-grace group is closed too");
    }
    Check(books.forgotten.size() == 2, "T-R2 both books are forgotten");
    Check(live.empty() && retired.empty(), "T-R2 the sampler's set and away clocks are dropped");
    for (Books::Call const& call : books.closed)
        Check(call.groupId != 303, "T-R2 a bound group's book is left to the bound path");
    Check(books.Fighting(303), "T-R2 the bound group's open fight survives the turn");
    Check(!books.Fighting(101), "T-R2 the observed group's fight is closed");

    // The turn is once: a second off tick closes nothing more.
    Check(!turn.TurnedOff(false), "T-R2 no second turn");
}

int main()
{
    TestSelectedCopy();
    TestBotCopy();
    TestOffTurn();
    if (g_failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_observed_raid_policy: all checks passed\n");
    return 0;
}
