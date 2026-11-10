#include "../ai/playerbot/CombatSpreadPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FleeHeadingDistance;
using ai::FleeFailureMemory;
using ai::IsRaidSpreadStayExempt;
using ai::IsSpreadExemptOwned;
using ai::IsSpreadOnCooldown;
using ai::kSpreadStepCooldownMs;
using ai::ShouldCombatSpread;
using ai::ShouldRaidSurvivalSpread;
using ai::ShouldSpreadStayVeto;

int main()
{
    std::cout << "Starting TortoiseBots combat-spread policy tests...\n";

    // Heading distance wraps: 0 and a full turn are the same heading, and
    // -PI/+PI are the same point.
    CHECK(FleeHeadingDistance(0.0f, 0.0f) == 0.0f);
    CHECK(FleeHeadingDistance(0.0f, 6.2831853072f) < 0.001f);
    CHECK(FleeHeadingDistance(3.1415926536f, -3.1415926536f) < 0.001f);
    CHECK(FleeHeadingDistance(0.0f, 3.1415926536f) > 3.14f);
    std::cout << "  [PASS] heading distance wraps at a full turn\n";

    // Dispatching an escape never vetoes it. Successful separation clears
    // the observation and the same heading may be used repeatedly.
    FleeFailureMemory success;
    for (unsigned i = 0; i < 5; ++i)
    {
        unsigned const now = 1000 + i * 1000;
        success.BeginAttempt(1, 0, 0.0f, 5.0f, now);
        CHECK(success.IsHeadingFree(0.0f, now));
        success.Observe(1, 0, 7.0f, now + 100);
        CHECK(!success.IsPending());
        CHECK(success.IsHeadingFree(0.0f, now + 100));
    }
    std::cout << "  [PASS] successful kiting repeatedly uses the same heading\n";

    FleeFailureMemory failed;
    failed.BeginAttempt(1, 0, 0.0f, 5.0f, 1000);
    failed.Observe(1, 0, 5.0f, 3999);
    CHECK(failed.IsHeadingFree(0.0f, 3999));
    failed.Observe(1, 0, 5.0f, 4000);
    CHECK(!failed.IsPending());
    CHECK(!failed.IsHeadingFree(0.0f, 4000));
    CHECK(!failed.IsHeadingFree(0.5f, 4000));
    CHECK(failed.IsHeadingFree(1.0f, 4000));
    CHECK(failed.IsHeadingFree(0.0f, 9000));
    std::cout << "  [PASS] only observed failures veto, then expire at 5 s\n";

    // Re-dispatching an unresolved heading must not postpone its verdict.
    FleeFailureMemory repeated;
    repeated.BeginAttempt(1, 0, 0.0f, 5.0f, 1000);
    repeated.BeginAttempt(1, 0, 0.0f, 5.0f, 2000);
    repeated.BeginAttempt(1, 0, 0.0f, 5.0f, 3000);
    repeated.Observe(1, 0, 5.0f, 4000);
    CHECK(!repeated.IsHeadingFree(0.0f, 4000));
    // The all-vetoed fallback can redeem a previously bad direction.
    repeated.BeginAttempt(1, 0, 0.0f, 5.0f, 4100);
    repeated.Observe(1, 0, 7.0f, 4200);
    CHECK(repeated.IsHeadingFree(0.0f, 4200));
    std::cout << "  [PASS] repeated dispatches do not hide failure; success redeems it\n";

    // Store normalized headings, including the second half of a full turn.
    FleeFailureMemory wrapped;
    wrapped.BeginAttempt(1, 0, 4.7123889804f, 5.0f, 1000);
    wrapped.Observe(1, 0, 5.0f, 4000);
    CHECK(!wrapped.IsHeadingFree(-1.5707963268f, 4000));
    wrapped.BeginAttempt(1, 0, 0.0f, 5.0f, 4100);
    wrapped.Observe(1, 0, 5.0f, 7100);
    CHECK(!wrapped.IsHeadingFree(0.0f, 7100));
    CHECK(!wrapped.IsHeadingFree(-1.5707963268f, 7100));
    CHECK(wrapped.IsHeadingFree((float)M_PI, 7100));
    std::cout << "  [PASS] both failed-heading slots work across angle wrap\n";

    // A different threat, a map transition, or an explicit clear loses all
    // old evidence. Observations too late to be attributable never veto.
    wrapped.Observe(2, 0, 5.0f, 7200);
    CHECK(wrapped.IsHeadingFree(0.0f, 7200));
    wrapped.BeginAttempt(2, 0, 0.0f, 5.0f, 7300);
    wrapped.Observe(2, 1, 5.0f, 10300);
    CHECK(wrapped.IsHeadingFree(0.0f, 10300));
    wrapped.BeginAttempt(2, 1, 0.0f, 5.0f, 10400);
    wrapped.Observe(2, 1, 5.0f, 15401);
    CHECK(wrapped.IsHeadingFree(0.0f, 15401));
    CHECK(!wrapped.IsPending());
    wrapped.BeginAttempt(2, 1, 0.0f, 5.0f, 16000);
    wrapped.Observe(2, 1, 5.0f, 19000);
    CHECK(!wrapped.IsHeadingFree(0.0f, 19000));
    wrapped.Clear();
    CHECK(wrapped.Anchor() == 0);
    CHECK(!wrapped.IsPending());
    CHECK(wrapped.IsHeadingFree(0.0f, 19000));
    std::cout << "  [PASS] threat/map/reset and stale observations cannot poison memory\n";

    FleeFailureMemory timerWrap;
    timerWrap.BeginAttempt(1, 0, 0.0f, 5.0f, 0xFFFFFF00u);
    timerWrap.Observe(1, 0, 5.0f, 0xFFFFFF00u + 3000u);
    CHECK(!timerWrap.IsHeadingFree(0.0f, 0xFFFFFF00u + 3000u));
    CHECK(timerWrap.IsHeadingFree(0.0f, 0xFFFFFF00u + 8000u));
    std::cout << "  [PASS] observation and expiry clocks tolerate 32-bit wrap\n";

    FleeFailureMemory interrupted;
    interrupted.BeginAttempt(1, 0, 0.0f, 5.0f, 1000, 10);
    interrupted.Observe(1, 0, 5.0f, 4000, 11);
    CHECK(!interrupted.IsPending());
    CHECK(interrupted.IsHeadingFree(0.0f, 4000));
    // Same-vector redispatches change the spline but retain the original
    // observation clock. An unrelated replacement instead discards it.
    interrupted.BeginAttempt(1, 0, 0.0f, 5.0f, 5000, 20);
    interrupted.BeginAttempt(1, 0, 0.0f, 5.0f, 6000, 21);
    interrupted.Observe(1, 0, 5.0f, 8000, 21);
    CHECK(!interrupted.IsHeadingFree(0.0f, 8000));
    std::cout << "  [PASS] replaced splines never manufacture failed headings\n";

    // Spread gate: combat only, pool bots only, no hold order.
    CHECK(ShouldCombatSpread(true, false, false, false, false, false));
    CHECK(!ShouldCombatSpread(false, false, false, false, false, false));
    CHECK(!ShouldCombatSpread(true, true, false, false, false, false));
    CHECK(!ShouldCombatSpread(true, false, true, false, false, false));
    CHECK(!ShouldCombatSpread(true, false, false, true, false, false));
    CHECK(!ShouldCombatSpread(true, false, false, false, true, false));
    CHECK(!ShouldCombatSpread(true, false, false, false, false, true));
    std::cout << "  [PASS] combat-only pool-bot spread gate\n";

    // Owned-bot answer: a live master exempts, and so does the owner record
    // when the master is offline or on another character. Pool bots pass.
    CHECK(IsSpreadExemptOwned(true, false));
    CHECK(IsSpreadExemptOwned(false, true));
    CHECK(IsSpreadExemptOwned(true, true));
    CHECK(!IsSpreadExemptOwned(false, false));
    std::cout << "  [PASS] offline-master owned bots stay exempt\n";

    // Raid survival spread: outside raid maps owned bots (live master or
    // owner record) hold; on a raid map they spread like pool bots. Combat
    // and explicit hold orders still gate everywhere.
    CHECK(ShouldRaidSurvivalSpread(true, false, false, false, false, false, false, false));
    CHECK(!ShouldRaidSurvivalSpread(true, true, false, false, false, false, false, false));
    CHECK(!ShouldRaidSurvivalSpread(true, false, true, false, false, false, false, false));
    CHECK(ShouldRaidSurvivalSpread(true, true, false, true, false, false, false, false));
    CHECK(ShouldRaidSurvivalSpread(true, false, true, true, false, false, false, false));
    CHECK(ShouldRaidSurvivalSpread(true, true, true, true, false, false, false, false));
    CHECK(!ShouldRaidSurvivalSpread(false, true, true, true, false, false, false, false));
    CHECK(!ShouldRaidSurvivalSpread(true, true, true, true, true, false, false, false));
    CHECK(!ShouldRaidSurvivalSpread(true, true, true, true, false, true, false, false));
    CHECK(!ShouldRaidSurvivalSpread(true, true, true, true, false, false, true, false));
    CHECK(!ShouldRaidSurvivalSpread(true, true, true, true, false, false, false, true));
    std::cout << "  [PASS] owned bots spread on raid maps, hold elsewhere\n";

    // Re-step throttle: no dispatch yet never throttles; a step inside the
    // window holds; a step past it goes. Wrap-safe at the 32-bit clock edge.
    CHECK(!IsSpreadOnCooldown(5000u, 0u));
    CHECK(IsSpreadOnCooldown(5000u, 5000u));
    CHECK(IsSpreadOnCooldown(5000u + kSpreadStepCooldownMs - 1, 5000u));
    CHECK(!IsSpreadOnCooldown(5000u + kSpreadStepCooldownMs, 5000u));
    CHECK(IsSpreadOnCooldown(10u, 0xFFFFFF00u));
    CHECK(!IsSpreadOnCooldown(100000u, 0xFFFFFF00u));
    std::cout << "  [PASS] spread re-step throttle\n";

    // RaidSpreadAction::isUseful's stay gate. Key off: the base movement
    // gate's refusal stands, whatever the caller's stay read, so the action
    // is exactly today's. Key on: the same stay term the trigger and Execute
    // pass to ShouldRaidSurvivalSpread is exempted - a stay with a live
    // spread row no longer refuses the step - and a stay with no live row
    // still does.
    CHECK(!ShouldSpreadStayVeto(false, true));
    CHECK(!ShouldSpreadStayVeto(false, false));
    CHECK(ShouldSpreadStayVeto(true, false));

    CHECK(!IsRaidSpreadStayExempt(false, true, true));
    CHECK(!IsRaidSpreadStayExempt(false, true, false));
    CHECK(!IsRaidSpreadStayExempt(false, false, true));
    CHECK(!IsRaidSpreadStayExempt(false, false, false));

    CHECK(IsRaidSpreadStayExempt(true, true, true));
    CHECK(!IsRaidSpreadStayExempt(true, true, false));
    CHECK(IsRaidSpreadStayExempt(true, false, true));
    CHECK(IsRaidSpreadStayExempt(true, false, false));
    std::cout << "  [PASS] raid spread stay gate reads the key and the live row\n";
    std::cout << "TortoiseBots combat-spread policy tests passed.\n";
    return 0;
}
