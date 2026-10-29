// The bound-group sampler's decisions and the fight's boss pick (task R1):
// a fight is fed by the sampler when its run did not feed it in the last 1.5 s,
// its lead-away clock closes a fight left open after 30 s, and the boss is the
// boss-flagged creature with the most maximum health (ties: the lowest entry);
// plain data and the standard library only.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_bound_fight_policy.cpp -o /tmp/bound_fight
//   /tmp/bound_fight

#include "../runtime/BoundFightPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace TortoiseBots::BoundFight;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestSamplerFeeds()
{
    CHECK(!SamplerFeeds(false, true, 0, 5000));    // not bound: never
    CHECK(!SamplerFeeds(false, false, 0, 5000));
    CHECK(SamplerFeeds(true, false, 0, 0));        // the run never fed it
    CHECK(!SamplerFeeds(true, true, 1000, 2000));  // fed 1000 ms ago: the run's
    CHECK(SamplerFeeds(true, true, 500, 2000));    // 1500 ms ago: the window's edge is the sampler's
    CHECK(!SamplerFeeds(true, true, 0xFFFFFF00u, 0x00000400u));   // 1280 ms across the tick wrap
}

// The open-fight clock: a lead away for the grace closes the fight it left
// open (unfinished), a lead back inside the grace keeps it, and the subtraction
// crosses a tick wrap.
static void TestLeadAwayGrace()
{
    LeadAway away;
    CHECK(!away.Overdue(false, 1000));     // the clock starts on the first unusable tick
    CHECK(!away.Overdue(false, 30999));    // 29 999 ms away: inside the 30 s grace
    CHECK(away.Overdue(false, 31000));     // 30 s: the fight is closed
    CHECK(away.Overdue(false, 40000));     // and stays due while the lead is gone

    LeadAway back;
    CHECK(!back.Overdue(false, 1000));
    CHECK(!back.Overdue(true, 5000));      // the lead is back (map transition done)
    CHECK(!back.Overdue(false, 20000));    // a new absence starts the clock again
    CHECK(!back.Overdue(false, 49999));
    CHECK(back.Overdue(false, 50000));

    LeadAway wrap;
    CHECK(!wrap.Overdue(false, 0xFFFFFF00u));
    CHECK(!wrap.Overdue(false, 0x00000400u));   // 1280 ms across the tick wrap
    CHECK(!wrap.Overdue(false, 0x00007300u));   // 29 696 ms: still inside
    CHECK(wrap.Overdue(false, 0x00007500u));    // 30 208 ms: due
}

static void TestPickBoss()
{
    std::vector<BossCandidate> none(2);
    none[0].entry = 10;
    none[0].maxHealth = 900;
    none[1].entry = 11;
    CHECK(PickBoss(none) == -1);
    CHECK(PickBoss({}) == -1);

    std::vector<BossCandidate> two(3);
    two[0].entry = 40;
    two[0].boss = true;
    two[0].maxHealth = 1200;
    two[1].entry = 30;   // an elite: never the boss
    two[1].maxHealth = 9000;
    two[2].entry = 20;
    two[2].boss = true;
    two[2].maxHealth = 8000;
    CHECK(PickBoss(two) == 2);

    std::vector<BossCandidate> tie(2);
    tie[0].entry = 60;
    tie[0].boss = true;
    tie[0].maxHealth = 5000;
    tie[1].entry = 50;
    tie[1].boss = true;
    tie[1].maxHealth = 5000;
    CHECK(PickBoss(tie) == 1);
}

int main()
{
    TestSamplerFeeds();
    TestLeadAwayGrace();
    TestPickBoss();
    std::printf("bound fight policy: %d checks passed\n", checks);
    return 0;
}
