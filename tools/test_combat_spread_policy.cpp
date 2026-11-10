#include "../ai/playerbot/CombatSpreadPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FleeHeadingDistance;
using ai::FleeFailureMemory;
using ai::IsSpreadExemptOwned;
using ai::IsSpreadOnCooldown;
using ai::kSpreadStepCooldownMs;
using ai::ShouldCombatSpread;
using ai::ShouldRaidSurvivalSpread;
using ai::SpreadSlotCastCutDue;
using ai::SpreadSlotFallback;
using ai::SpreadSlotIndexOf;
using ai::SpreadSlotOrder;
using ai::SpreadSlotPoint;
using ai::SpreadSlotPoints;
using ai::SpreadSlotRings;
using ai::SpreadSlotSelect;
using ai::SpreadSlotVerdict;
using ai::SpreadSlotVerdictWord;
using ai::SpreadSlotWanted;

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

    // Fixed slots: rings fit the anchor's circle and never pass the cap.
    CHECK(SpreadSlotRings(12.0f, true, 30.0f) == 2);
    CHECK(SpreadSlotRings(12.0f, true, 13.9f) == 0);
    CHECK(SpreadSlotRings(12.0f, true, 14.0f) == 1);
    CHECK(SpreadSlotRings(12.0f, false, 0.0f) == 2);
    CHECK(SpreadSlotRings(5.0f, true, 60.0f) == 3);
    CHECK(SpreadSlotRings(0.0f, true, 30.0f) == 0);
    std::cout << "  [PASS] slot rings fit the anchor and cap at three\n";

    // Two rings of 6 and 12 points, the row's spacing apart, none past 24 yd.
    {
        const std::vector<SpreadSlotPoint> points = SpreadSlotPoints(0.0f, 0.0f, 12.0f, 2);
        float closest = 1e9f;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            CHECK(std::sqrt(points[i].x * points[i].x + points[i].y * points[i].y) <= 24.01f);
            for (std::size_t j = i + 1; j < points.size(); ++j)
            {
                const float dx = points[i].x - points[j].x;
                const float dy = points[i].y - points[j].y;
                const float yd = std::sqrt(dx * dx + dy * dy);
                if (yd < closest)
                    closest = yd;
                CHECK(yd >= 11.9f);
            }
        }
        CHECK(points.size() == 18);
        CHECK(closest > 11.9f && closest < 12.1f);
    }
    std::cout << "  [PASS] the 18 slot points keep the row's spacing\n";

    // Nearest to the preference point first; an equal distance keeps the lower j.
    {
        std::vector<SpreadSlotPoint> points = SpreadSlotPoints(0.0f, 0.0f, 12.0f, 2);
        SpreadSlotOrder(points, 0.0f, -15.0f);
        const auto prefYd = [](const SpreadSlotPoint& point) {
            return std::sqrt(point.x * point.x + (point.y + 15.0f) * (point.y + 15.0f));
        };
        CHECK(points[0].ring == 1 && points[0].j == 4);
        CHECK(std::fabs(prefYd(points[0]) - 7.57f) <= 0.05f);
        CHECK(points[1].ring == 1 && points[1].j == 5);
        CHECK(std::fabs(prefYd(points[12]) - 31.42f) <= 0.05f);
        for (std::size_t i = 1; i < points.size(); ++i)
            CHECK(prefYd(points[i - 1]) <= prefYd(points[i]) + 0.001f);
    }
    std::cout << "  [PASS] the slot order runs nearest to the seek spot first\n";

    // The index counts lower guids only, and a bot outside the list has none.
    const std::vector<std::uint64_t> guids = { 50, 10, 30 };
    CHECK(SpreadSlotIndexOf(guids, 10) == 0);
    CHECK(SpreadSlotIndexOf(guids, 30) == 1);
    CHECK(SpreadSlotIndexOf(guids, 50) == 2);
    CHECK(SpreadSlotIndexOf(guids, 99) == -1);
    CHECK(SpreadSlotFallback(0, 13, 18) == 13);
    CHECK(SpreadSlotFallback(5, 13, 18) == 13);
    CHECK(SpreadSlotFallback(7, 13, 18) == 15);
    CHECK(SpreadSlotFallback(0, 18, 18) == -1);
    std::cout << "  [PASS] slots are stable and the spare slots catch the overflow\n";

    // SpreadSlot_FallbackKeepsOwnedTelemetry: the row reports the owned
    // slot and its distance, while the walk goes to the spare point.
    {
        const ai::SpreadSlotSelection here = SpreadSlotSelect(7, 15, true, false);
        CHECK(here.slot == 7 && here.moveSlot == 7 && here.fallback == -1);
        const ai::SpreadSlotSelection spare = SpreadSlotSelect(7, 15, false, true);
        CHECK(spare.slot == 7);
        CHECK(spare.moveSlot == 15);
        CHECK(spare.fallback == 15);
        const ai::SpreadSlotSelection refused = SpreadSlotSelect(7, 15, false, false);
        CHECK(refused.slot == 7 && refused.moveSlot == -1 && refused.fallback == -1);
        const ai::SpreadSlotSelection none = SpreadSlotSelect(7, -1, false, false);
        CHECK(none.slot == 7 && none.moveSlot == -1 && none.fallback == -1);
    }
    std::cout << "  [PASS] fallback keeps the owned slot telemetry\n";

    // The verdict precedence of the slot decision, in D6 order.
    CHECK(SpreadSlotWanted(true, true, true, true, false, false, false, false, true, true) ==
          SpreadSlotVerdict::Seek);
    CHECK(SpreadSlotWanted(false, true, true, true, true, true, true, true, false, false) ==
          SpreadSlotVerdict::Stand);
    CHECK(SpreadSlotWanted(false, false, true, true, true, true, true, true, false, false) ==
          SpreadSlotVerdict::Victim);
    CHECK(SpreadSlotWanted(false, false, false, true, true, true, true, true, false, false) ==
          SpreadSlotVerdict::Walkout);
    CHECK(SpreadSlotWanted(false, false, false, false, false, true, true, true, false, false) ==
          SpreadSlotVerdict::NoCentre);
    CHECK(SpreadSlotWanted(false, false, false, false, true, false, true, true, false, false) ==
          SpreadSlotVerdict::NoSlot);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, false, true, false, false) ==
          SpreadSlotVerdict::Floor);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, false, false, false) ==
          SpreadSlotVerdict::Range);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, true, false, true) ==
          SpreadSlotVerdict::SlotHold);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, true, true, false) ==
          SpreadSlotVerdict::Cast);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, true, false, false) ==
          SpreadSlotVerdict::SlotMove);
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::SlotHold)) == "slot_hold");
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::NoCentre)) == "no_centre");
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::MoveFailed)) == "move_failed");
    std::cout << "  [PASS] slot verdict precedence and words\n";

    // A heal is never cut; a damage dealer cuts after 6 s of blocked reads.
    CHECK(!SpreadSlotCastCutDue(true, 1000u, 100000u));
    CHECK(!SpreadSlotCastCutDue(false, 0u, 100000u));
    CHECK(!SpreadSlotCastCutDue(false, 5000u, 5000u + 5999u));
    CHECK(SpreadSlotCastCutDue(false, 5000u, 5000u + 6000u));
    CHECK(SpreadSlotCastCutDue(false, 0xFFFFFF00u, 0xFFFFFF00u + 6000u));
    std::cout << "  [PASS] only a damage dealer's cast is cut, after 6 s\n";

    std::cout << "TortoiseBots combat-spread policy tests passed.\n";
    return 0;
}
