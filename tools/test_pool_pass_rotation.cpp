// Standalone regression test for the random-pool AI pass rotation
// (runtime/PoolPassRotation.h): the fairness mechanism behind the per-tick
// pool budget. Exercises the rules that must not regress — a full pass visits
// every pool bot exactly once, a budgeted pass resumes where the previous one
// stopped instead of skipping bots, the budget only bites while it is active,
// and a membership change rebuilds the order (including the same-size case)
// while an unchanged pool keeps its cursor.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pool_pass_rotation.cpp -o /tmp/test_pool_pass_rotation
//   /tmp/test_pool_pass_rotation

#include "../runtime/PoolPassRotation.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// In-pool predicate over a fixed live set, as BotManager builds it from m_bots.
static std::set<uint32_t> g_live;
static bool InPool(uint32_t guidLow) { return g_live.count(guidLow) != 0; }

// Fake monotonic clock charged by the update callback, so a test can decide
// exactly how much module work each bot costs.
struct FakeClock
{
    uint64_t nowUs = 100000;
    uint64_t Now() const { return nowUs; }
    void Charge(uint64_t costUs) { nowUs += costUs; }
};

static void TestFullPassVisitsEveryBotOnce()
{
    PoolPassRotation rotation;
    std::vector<uint32_t> pool = {10, 20, 30, 40, 50};
    g_live = {10, 20, 30, 40, 50};
    rotation.Refresh(pool, InPool);
    CHECK(rotation.Size() == 5);

    FakeClock clock;
    std::vector<uint32_t> visited;
    bool budgetHit = true;
    uint32_t processed = rotation.Run(false, 1,
        [&clock]() { return clock.Now(); },
        [&](uint32_t guidLow) { visited.push_back(guidLow); clock.Charge(5000); },
        budgetHit);

    // No active budget: the whole pool runs even though the clock sails past a
    // one-microsecond budget, and that is not reported as a budget stop.
    CHECK(processed == 5);
    CHECK(visited.size() == 5);
    CHECK(!budgetHit);
    CHECK(std::set<uint32_t>(visited.begin(), visited.end()) == g_live);

    // A second full pass starts over at the same place: no drift, no skipped
    // tail, no duplicate visit.
    std::vector<uint32_t> second;
    processed = rotation.Run(false, 1,
        [&clock]() { return clock.Now(); },
        [&](uint32_t guidLow) { second.push_back(guidLow); },
        budgetHit);
    CHECK(processed == 5);
    CHECK(second == visited);
}

static void TestBudgetResumesAndNeverSkips()
{
    PoolPassRotation rotation;
    std::vector<uint32_t> pool;
    for (uint32_t i = 1; i <= 12; ++i)
        pool.push_back(i * 100);
    g_live = std::set<uint32_t>(pool.begin(), pool.end());
    rotation.Refresh(pool, InPool);

    FakeClock clock;
    uint64_t const costPerBotUs = 4000;
    uint64_t const budgetUs = 10000; // exactly three bots per pass
    std::set<uint32_t> visitedThisCycle;
    uint32_t passes = 0;

    while (visitedThisCycle.size() < pool.size())
    {
        bool budgetHit = false;
        rotation.Run(true, budgetUs,
            [&clock]() { return clock.Now(); },
            [&](uint32_t guidLow)
            {
                // A bot must never repeat before every pool bot has had a turn.
                CHECK(visitedThisCycle.insert(guidLow).second);
                clock.Charge(costPerBotUs);
            },
            budgetHit);
        CHECK(budgetHit);
        ++passes;
        CHECK(passes <= 4);
    }

    CHECK(passes == 4);
    CHECK(visitedThisCycle.size() == pool.size());
}

static void TestBudgetNotHitWhenItCoversThePool()
{
    PoolPassRotation rotation;
    std::vector<uint32_t> pool = {7, 8, 9, 11};
    g_live = {7, 8, 9, 11};
    rotation.Refresh(pool, InPool);

    FakeClock clock;
    uint32_t processed = 0;
    bool budgetHit = true;
    processed = rotation.Run(true, 100000,
        [&clock]() { return clock.Now(); },
        [&](uint32_t) { clock.Charge(1000); },
        budgetHit);

    CHECK(processed == 4);
    CHECK(!budgetHit);
}

static void TestUnchangedMembershipKeepsCursor()
{
    PoolPassRotation rotation;
    std::vector<uint32_t> pool = {1, 2, 3, 4};
    g_live = {1, 2, 3, 4};
    rotation.Refresh(pool, InPool);

    FakeClock clock;
    bool budgetHit = false;
    uint32_t processed = rotation.Run(true, 10,
        [&clock]() { return clock.Now(); },
        [&](uint32_t) { clock.Charge(10); },
        budgetHit);
    CHECK(processed == 1);
    CHECK(budgetHit);

    // Same membership: the refresh must not reset the cursor, so the next pass
    // continues with the second bot instead of re-running the first one.
    rotation.Refresh(pool, InPool);
    CHECK(rotation.Size() == 4);

    std::vector<uint32_t> visited;
    rotation.Run(false, 0,
        [&clock]() { return clock.Now(); },
        [&](uint32_t guidLow) { visited.push_back(guidLow); },
        budgetHit);
    CHECK(visited.size() == 4);
    CHECK(visited.front() == 2);
}

static void TestMembershipChangeRebuilds()
{
    PoolPassRotation rotation;
    std::vector<uint32_t> pool = {1, 2, 3};
    g_live = {1, 2, 3};
    rotation.Refresh(pool, InPool);

    FakeClock clock;
    bool budgetHit = false;
    rotation.Run(true, 10,
        [&clock]() { return clock.Now(); },
        [&](uint32_t) { clock.Charge(10); },
        budgetHit);
    CHECK(rotation.Size() == 3);

    // Same size, different members: bot 2 leaves, bot 4 joins. The rebuild must
    // follow the live list and drop the departed bot for good.
    pool = {4, 1, 3};
    g_live = {1, 3, 4};
    rotation.Refresh(pool, InPool);
    CHECK(rotation.Size() == 3);

    std::vector<uint32_t> visited;
    rotation.Run(false, 0,
        [&clock]() { return clock.Now(); },
        [&](uint32_t guidLow) { visited.push_back(guidLow); },
        budgetHit);
    CHECK(visited == pool);
    CHECK(std::set<uint32_t>(visited.begin(), visited.end()) == g_live);
}

static void TestEmptyPool()
{
    PoolPassRotation rotation;
    std::vector<uint32_t> pool;
    g_live.clear();
    rotation.Refresh(pool, InPool);
    CHECK(rotation.Size() == 0);

    bool budgetHit = true;
    uint32_t processed = rotation.Run(true, 10000, []() { return 0; }, [](uint32_t) {}, budgetHit);
    CHECK(processed == 0);
    CHECK(!budgetHit);
}

static void TestSpentBudgetStartsNoNewBot()
{
    // Budget already spent (zero budget): the pass must start no bot, report
    // the hit, and leave the cursor unmoved so the next pass retries the
    // same first bot instead of skipping it.
    PoolPassRotation rotation;
    std::vector<uint32_t> pool = {1, 2, 3};
    g_live = {1, 2, 3};
    rotation.Refresh(pool, InPool);

    FakeClock clock;
    bool budgetHit = false;
    std::vector<uint32_t> visited;
    uint32_t processed = rotation.Run(true, 0,
        [&clock]() { return clock.Now(); },
        [&](uint32_t guidLow) { visited.push_back(guidLow); },
        budgetHit);
    CHECK(processed == 0);
    CHECK(visited.empty());
    CHECK(budgetHit);

    // Cursor unmoved: a full pass still starts at the first bot.
    rotation.Run(false, 0,
        [&clock]() { return clock.Now(); },
        [&](uint32_t guidLow) { visited.push_back(guidLow); },
        budgetHit);
    CHECK(visited == pool);
}

int main()
{
    TestFullPassVisitsEveryBotOnce();
    TestBudgetResumesAndNeverSkips();
    TestBudgetNotHitWhenItCoversThePool();
    TestUnchangedMembershipKeepsCursor();
    TestMembershipChangeRebuilds();
    TestEmptyPool();
    TestSpentBudgetStartsNoNewBot();

    std::printf("PASSED: pool pass rotation (%d checks)\n", checks);
    return 0;
}
