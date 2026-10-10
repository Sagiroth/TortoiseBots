// Standalone regression test for the self-tuning tick budget
// (runtime/AdaptiveBudget.h): the feedback controller behind the per-tick
// pool/combat budgets. Exercises the rules that must not regress — off holds
// the ceilings, sustained over-target ticks shrink multiplicatively toward
// the floor, sustained under-target ticks grow additively back to the
// ceiling, the deadband holds steady (no oscillation), a single spike barely
// moves the EMA, and the floor/ceiling clamps hold under long pressure.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_adaptive_budget.cpp -o /tmp/test_adaptive_budget
//   /tmp/test_adaptive_budget

#include "../runtime/AdaptiveBudget.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Drive the controller for n ticks at a fixed measured tick time.
static void Drive(AdaptiveBudget& budget, uint32_t tickMs, int n)
{
    for (int i = 0; i < n; ++i)
        budget.Update(tickMs);
}

static void TestOffHoldsCeilings()
{
    AdaptiveBudget budget(0, 10000, 15000);
    Drive(budget, 500, 100);
    CHECK(budget.PoolUs() == 10000);
    CHECK(budget.CombatUs() == 15000);
}

static void TestShrinkIsMultiplicativeAndFloors()
{
    AdaptiveBudget budget(50, 10000, 15000);
    Drive(budget, 10, 50);
    CHECK(budget.PoolUs() == 10000);
    // Over target: geometric retreat, ~3 ticks to halve (0.7^3 ~= 0.34).
    Drive(budget, 300, 3);
    CHECK(budget.PoolUs() < 5000);
    uint64_t const afterThree = budget.PoolUs();
    Drive(budget, 300, 1);
    // Each further tick multiplies, not subtracts: the step itself shrinks.
    CHECK(budget.PoolUs() < afterThree);
    CHECK(afterThree - budget.PoolUs() < 10000 - afterThree + 1);
    // Long overload parks at the floor, never at zero (a zero budget would
    // serve nobody while still paying the rotation scan).
    Drive(budget, 500, 200);
    CHECK(budget.PoolUs() == 2000);
    CHECK(budget.CombatUs() == 2000);
}

static void TestGrowIsAdditiveAndCeilings()
{
    AdaptiveBudget budget(50, 100000, 100000);
    Drive(budget, 300, 30);
    CHECK(budget.PoolUs() < 100000);
    CHECK(budget.SmoothedTickMs() > 60.0);
    // The EMA must decay back into the band before reclaim starts: settle it,
    // then two consecutive windows must grow by exactly the step (linear
    // reclaim, no overshoot sawtooth).
    Drive(budget, 10, 60);
    CHECK(budget.SmoothedTickMs() < 40.0);
    uint64_t const a = budget.PoolUs();
    CHECK(a > 2000);
    Drive(budget, 10, 4);
    uint64_t const b = budget.PoolUs();
    CHECK(b - a == 4 * 1000);
    CHECK(b < 100000);
    // Long healthy stretch returns exactly to the operator's ceiling.
    Drive(budget, 10, 100);
    CHECK(budget.PoolUs() == 100000);
    CHECK(budget.CombatUs() == 100000);
}

static void TestDeadbandHoldsSteady()
{
    AdaptiveBudget budget(50, 10000, 15000);
    Drive(budget, 10, 50);
    CHECK(budget.PoolUs() == 10000);
    // 40-60 ms sits inside the 50+-10 deadband: no movement either way.
    Drive(budget, 45, 50);
    CHECK(budget.PoolUs() == 10000);
    Drive(budget, 58, 50);
    CHECK(budget.PoolUs() == 10000);
    // Just outside the band it moves again.
    Drive(budget, 75, 20);
    CHECK(budget.PoolUs() < 10000);
}

static void TestSingleSpikeBarelyMovesEma()
{
    AdaptiveBudget budget(50, 10000, 15000);
    Drive(budget, 40, 50);
    double const calm = budget.SmoothedTickMs();
    CHECK(calm < 50.0);
    // One bad tick (a 300 ms hitch on a 40 ms baseline) moves the EMA by 1/8
    // of the gap — just outside the deadband, so the budget shrinks a few
    // steps instead of collapsing. Calm ticks then decay the EMA back into
    // the band and reclaim starts again.
    budget.Update(300);
    CHECK(budget.SmoothedTickMs() < calm + (300.0 - calm) / 8.0 + 1.0);
    uint64_t const afterSpike = budget.PoolUs();
    CHECK(afterSpike < 10000);
    CHECK(afterSpike > 2000);
    Drive(budget, 40, 60);
    CHECK(budget.SmoothedTickMs() < 45.0);
    // Once the EMA settles inside the band the budget holds: no snap-back,
    // no further shrink while the reading stays in-band.
    uint64_t const settled = budget.PoolUs();
    Drive(budget, 40, 60);
    CHECK(budget.PoolUs() == settled);
    Drive(budget, 10, 200);
    CHECK(budget.PoolUs() > settled);
}

static void TestConvergesWithoutOscillation()
{
    // Alternating either side of the deadband must settle, not sawtooth:
    // shrink steps (x0.7 of ~10 ms) outweigh grow steps (+1 ms), so the
    // budget ratchets down into the band and holds.
    AdaptiveBudget budget(50, 22000, 22000);
    Drive(budget, 10, 100);
    CHECK(budget.PoolUs() == 22000);
    for (int i = 0; i < 60; ++i)
    {
        Drive(budget, 200, 2);
        Drive(budget, 10, 4);
    }
    uint64_t const settled = budget.PoolUs();
    // Still well below the ceiling (pressure dominated), and a further
    // identical cycle barely moves it: converged, not oscillating.
    CHECK(settled < 22000);
    for (int i = 0; i < 10; ++i)
    {
        Drive(budget, 200, 2);
        Drive(budget, 10, 4);
    }
    uint64_t const later = budget.PoolUs();
    CHECK(later >= (settled > 3000 ? settled - 3000 : 2000));
    CHECK(later <= settled + 3000);
}

int main()
{
    TestOffHoldsCeilings();
    TestShrinkIsMultiplicativeAndFloors();
    TestGrowIsAdditiveAndCeilings();
    TestDeadbandHoldsSteady();
    TestSingleSpikeBarelyMovesEma();
    TestConvergesWithoutOscillation();
    std::printf("adaptive budget: all %d checks passed\n", checks);
    return 0;
}
