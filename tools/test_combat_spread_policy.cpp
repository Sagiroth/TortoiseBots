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
using ai::IsRaidSpreadStayExempt;
using ai::IsSpreadExemptOwned;
using ai::IsSpreadOnCooldown;
using ai::kSpreadStepCooldownMs;
using ai::ShouldCombatSpread;
using ai::ShouldRaidSurvivalSpread;
using ai::ShouldSpreadStayVeto;
using ai::SpreadLogCaller;
using ai::SpreadLogOwned;
using ai::SpreadCastClock;
using ai::SpreadCentreFor;
using ai::SpreadCentreKind;
using ai::SpreadCentreWord;
using ai::kSpreadMinRangeShotYd;
using ai::kSpreadSlotArriveYd;
using ai::SpreadSlotBossGrid;
using ai::SpreadSlotCastCutDue;
using ai::SpreadSlotCastStart;
using ai::SpreadSlotFallback;
using ai::SpreadSlotIndexOf;
using ai::SpreadSlotInsideMinRange;
using ai::SpreadSlotMinRangeApplies;
using ai::SpreadSlotMinRangePoint;
using ai::SpreadSlotMinRangeYd;
using ai::SpreadSlotOrder;
using ai::SpreadSlotOwnerPoints;
using ai::SpreadSlotPoint;
using ai::SpreadSlotPoints;
using ai::SpreadSlotRings;
using ai::SpreadSlotSelect;
using ai::SpreadSlotSpacingAngle;
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

    // SpreadSlot_HunterSlotLeavesDeadZone: the rings have no floor from the boss, so a hunter's slot can sit inside
    // Auto Shot's 8 yd dead zone. With the key on and a class that needs a minimum range, a slot nearer the boss
    // than the boss's reach + 8 + 2 yd moves out radially to exactly that range; a slot already that far keeps its
    // point, and the key off (or a caster) hands the ring back untouched.
    CHECK(!SpreadSlotMinRangeApplies(false, true));
    CHECK(!SpreadSlotMinRangeApplies(true, false));
    CHECK(SpreadSlotMinRangeApplies(true, true));
    // The scan step is the angle that separates two points by the row's spacing on the min-range circle.
    CHECK(SpreadSlotSpacingAngle(0.0f, 15.0f) == 0.0f);
    CHECK(SpreadSlotSpacingAngle(10.0f, 0.0f) == 0.0f);
    CHECK(std::fabs(SpreadSlotSpacingAngle(10.0f, 15.0f) - 2.0f * std::asin(1.0f / 3.0f)) < 0.0001f);
    CHECK(std::fabs(SpreadSlotSpacingAngle(40.0f, 15.0f) - 3.1415926536f) < 0.0001f);
    // The floor: the boss's body + Auto Shot's dead zone + the hunter's own body + the arrival slack + 1 yd.
    CHECK(std::fabs(SpreadSlotMinRangeYd(0.0f, 0.0f) - 11.0f) < 0.001f);
    CHECK(std::fabs(SpreadSlotMinRangeYd(9.0f, 1.5f) - 21.5f) < 0.001f);
    CHECK(std::fabs(SpreadSlotMinRangeYd(-5.0f, -2.0f) - 11.0f) < 0.001f);
    CHECK(std::fabs(SpreadSlotMinRangeYd(3.0f, 1.5f) - SpreadSlotMinRangeYd(3.0f, 0.0f) - 1.5f) < 0.001f);
    // SlotInsideHunterMinRange: the runtime guard the spread read uses, independent of the floor. A slot 10 yd from
    // the target centre with 7.5 yd of reach between the two bodies leaves 2.5 yd of shot, inside Auto Shot's 8 yd
    // minimum plus the 2 yd margin; at 25 yd it leaves 17.5 yd and clears. A class with no minimum range never bites.
    CHECK(SpreadSlotInsideMinRange(10.0f, 7.5f, 8.0f));
    CHECK(!SpreadSlotInsideMinRange(25.0f, 7.5f, 8.0f));
    CHECK(!SpreadSlotInsideMinRange(0.0f, 0.0f, 0.0f));
    CHECK(SpreadSlotInsideMinRange(9.99f, 0.0f, 8.0f));  // inside by the margin alone
    CHECK(!SpreadSlotInsideMinRange(10.0f, 0.0f, 8.0f));  // exactly at min + margin: outside
    CHECK(SpreadSlotInsideMinRange(14.9f, 5.0f, 8.0f));  // 9.9 yd of shot, still inside
    CHECK(!SpreadSlotInsideMinRange(15.1f, 5.0f, 8.0f));  // 10.1 yd of shot, outside
    // SpreadSlot_ArrivedHunterStaysOutOfTheDeadZone: HunterTargetInDeadZone takes both combat reaches off the target
    // distance and compares the rest with Auto Shot's 8 yd minimum. A hunter reads as arrived while up to
    // kSpreadSlotArriveYd inside its point, so the floor must clear the dead zone by that slack plus its own margin.
    for (const float bossReach : { 0.0f, 5.0f, 10.0f, 14.0f })
        for (const float selfReach : { 0.0f, 1.5f, 2.5f })
        {
            const float floorYd = SpreadSlotMinRangeYd(bossReach, selfReach);
            const float arrivedYd = floorYd - kSpreadSlotArriveYd;
            CHECK(arrivedYd - bossReach - selfReach > kSpreadMinRangeShotYd);
        }
    {
        const SpreadSlotPoint inside{12.0f, 0.0f, 1, 0};
        const SpreadSlotPoint out = SpreadSlotMinRangePoint(inside, 0.0f, 0.0f, 0.0f, 0.0f, 19.0f);
        CHECK(std::fabs(out.x - 19.0f) < 0.001f && std::fabs(out.y) < 0.001f);
        CHECK(out.ring == 1 && out.j == 0);
        const SpreadSlotPoint outside{0.0f, 24.0f, 2, 3};
        const SpreadSlotPoint kept = SpreadSlotMinRangePoint(outside, 0.0f, 0.0f, 0.0f, 0.0f, 19.0f);
        CHECK(kept.x == 0.0f && kept.y == 24.0f);
        const SpreadSlotPoint off = SpreadSlotMinRangePoint(inside, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        CHECK(off.x == 12.0f && off.y == 0.0f);
        // An off-centre boss: the bearing is the boss->slot one, so the point lands exactly minYd from the boss.
        const SpreadSlotPoint bossOff = SpreadSlotMinRangePoint(inside, 3.0f, 0.0f, 0.0f, 0.0f, 14.0f);
        const float dx = bossOff.x - 3.0f;
        CHECK(std::fabs(std::sqrt(dx * dx + bossOff.y * bossOff.y) - 14.0f) < 0.001f);
        // A slot on the boss itself takes the centre's bearing instead, and always lands on the floor.
        const SpreadSlotPoint onBoss{5.0f, 5.0f, 0, 0};
        const SpreadSlotPoint pushed = SpreadSlotMinRangePoint(onBoss, 5.0f, 5.0f, 0.0f, 0.0f, 19.0f);
        const float px = pushed.x - 5.0f;
        const float py = pushed.y - 5.0f;
        CHECK(std::fabs(std::sqrt(px * px + py * py) - 19.0f) < 0.001f);
        CHECK(pushed.x > 5.0f && pushed.y > 5.0f);
    }
    std::cout << "  [PASS] a min-range slot outside the dead zone, the key off and a caster keep the ring\n";

    // SpreadSlot_MixedGroupSpacingSurvivesProjection: two hunters and two casters each build and order the same
    // canonical list around an off-centre boss, then resolve the owner points the way every member does. Production
    // sizing throughout: the ring holds all 36 points and the flags cover all of them, but only the 4 the group owns
    // are obstacles. All four compute the same final array; a hunter steps off its bearing when the radial projection
    // would land inside the row's spacing of another owner's point; and no two final points are closer than the
    // spacing, while empty ring points never push anyone.
    {
        const float spacingYd = 10.0f;
        const float cx = 0.0f;
        const float cy = 0.0f;
        const float bossX = 6.0f;   // the boss stands off the ring centre
        const float bossY = -4.0f;
        const float prefX = bossX + 12.0f;
        const float prefY = bossY - 8.0f;
        const std::uint64_t hunterGuid = 10;
        const std::uint64_t secondHunterGuid = 20;
        const std::uint64_t mageGuid = 30;
        const std::uint64_t priestGuid = 40;
        const std::vector<std::uint64_t> eligible = { hunterGuid, secondHunterGuid, mageGuid, priestGuid };
        const auto canonical = [&]() {
            std::vector<SpreadSlotPoint> points =
                SpreadSlotPoints(cx, cy, spacingYd, SpreadSlotRings(spacingYd, true, 60.0f));
            SpreadSlotOrder(points, prefX, prefY);
            return points;
        };
        const std::vector<SpreadSlotPoint> shared = canonical();
        CHECK(shared.size() == 36);
        // Every member builds the same class-independent order.
        for (const std::vector<SpreadSlotPoint>& same : {canonical(), canonical(), canonical()})
            for (std::size_t i = 0; i < shared.size(); ++i)
                CHECK(shared[i].x == same[i].x && shared[i].y == same[i].y && shared[i].ring == same[i].ring &&
                      shared[i].j == same[i].j);
        const int hunterIndex = SpreadSlotIndexOf(eligible, hunterGuid);
        const int secondHunterIndex = SpreadSlotIndexOf(eligible, secondHunterGuid);
        const int mageIndex = SpreadSlotIndexOf(eligible, mageGuid);
        const int priestIndex = SpreadSlotIndexOf(eligible, priestGuid);
        CHECK(hunterIndex == 0 && secondHunterIndex == 1 && mageIndex == 2 && priestIndex == 3);
        const std::size_t ownerCount = eligible.size();
        std::vector<char> minRangeByIndex(shared.size(), 0);
        minRangeByIndex[0] = 1;
        minRangeByIndex[1] = 1;
        const auto apart = [](const SpreadSlotPoint& a, const SpreadSlotPoint& b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        };
        const auto bossYd = [&](const SpreadSlotPoint& point) {
            const float dx = point.x - bossX;
            const float dy = point.y - bossY;
            return std::sqrt(dx * dx + dy * dy);
        };
        // Realistic boss reaches: the min-range circle moves from the inner ring out past the outer one, and the
        // owner points keep the spacing at every one of them.
        for (const float reach : { 0.0f, 5.0f, 10.0f, 14.0f })
        {
            const float minYd = SpreadSlotMinRangeYd(reach, 1.5f);
            const std::vector<SpreadSlotPoint> owners =
                SpreadSlotOwnerPoints(shared, minRangeByIndex, ownerCount, bossX, bossY, cx, cy, minYd, spacingYd);
            // One point per owner, never one per ring point: the unowned ring is empty ground.
            CHECK(owners.size() == ownerCount);
            // The same group yields the same points on every member: each one resolves from its own fresh list.
            for (int member = 0; member < 4; ++member)
            {
                const std::vector<SpreadSlotPoint> rebuilt = SpreadSlotOwnerPoints(
                    canonical(), minRangeByIndex, ownerCount, bossX, bossY, cx, cy, minYd, spacingYd);
                CHECK(rebuilt.size() == owners.size());
                for (std::size_t i = 0; i < owners.size(); ++i)
                    CHECK(rebuilt[i].x == owners[i].x && rebuilt[i].y == owners[i].y);
            }
            // Inner-to-outer ring pairs included: no projected point inside the spread's own spacing.
            for (std::size_t i = 0; i < owners.size(); ++i)
                for (std::size_t j = i + 1; j < owners.size(); ++j)
                    CHECK(apart(owners[i], owners[j]) >= spacingYd - 0.001f);
            for (const int hunterSlot : { hunterIndex, secondHunterIndex })
            {
                const SpreadSlotPoint canonicalPoint = shared[static_cast<std::size_t>(hunterSlot)];
                const SpreadSlotPoint ownerPoint = owners[static_cast<std::size_t>(hunterSlot)];
                CHECK(bossYd(ownerPoint) >= minYd - 0.001f);
                if (bossYd(canonicalPoint) < minYd - 0.001f)
                    CHECK(ownerPoint.x != canonicalPoint.x || ownerPoint.y != canonicalPoint.y);
            }
            for (const int casterSlot : { mageIndex, priestIndex })
                CHECK(owners[static_cast<std::size_t>(casterSlot)].x ==
                          shared[static_cast<std::size_t>(casterSlot)].x &&
                      owners[static_cast<std::size_t>(casterSlot)].y ==
                          shared[static_cast<std::size_t>(casterSlot)].y);
        }
    }
    std::cout << "  [PASS] two hunters and two casters keep the spacing after the projection\n";

    // SpreadSlot_BossDriftKeepsTheMap: every member reads the live boss on its own tick, so a drift of a fraction of a
    // yard between two reads must still resolve to one map. The position is snapped to the 1 yd grid before the owner
    // points are built, and a 0.6 yd drift over the test's boss (6,-4) stays in the same cell.
    {
        const float spacingYd = 10.0f;
        const float minYd = SpreadSlotMinRangeYd(5.0f, 1.5f);
        std::vector<SpreadSlotPoint> shared = SpreadSlotPoints(0.0f, 0.0f, spacingYd,
                                                               SpreadSlotRings(spacingYd, true, 60.0f));
        SpreadSlotOrder(shared, 18.0f, -12.0f);
        std::vector<char> minRangeByIndex(shared.size(), 0);
        minRangeByIndex[0] = 1;
        CHECK(SpreadSlotBossGrid(6.0f) == SpreadSlotBossGrid(6.6f));
        CHECK(SpreadSlotBossGrid(-4.0f) == SpreadSlotBossGrid(-3.4f));
        const std::vector<SpreadSlotPoint> here = SpreadSlotOwnerPoints(
            shared, minRangeByIndex, 4, SpreadSlotBossGrid(6.0f), SpreadSlotBossGrid(-4.0f), 0.0f, 0.0f, minYd,
            spacingYd);
        const std::vector<SpreadSlotPoint> drifted = SpreadSlotOwnerPoints(
            shared, minRangeByIndex, 4, SpreadSlotBossGrid(6.6f), SpreadSlotBossGrid(-3.4f), 0.0f, 0.0f, minYd,
            spacingYd);
        CHECK(here.size() == 4 && drifted.size() == here.size());
        for (std::size_t i = 0; i < here.size(); ++i)
            CHECK(here[i].x == drifted[i].x && here[i].y == drifted[i].y);
    }
    std::cout << "  [PASS] a fractional boss drift still resolves to one slot map\n";

    // SpreadSlot_MinRangeFallbackLeastCrowded: an artificially dense min-range circle (a caster every 39 degrees on
    // it) leaves every stepped candidate inside the spacing. The resolver then takes the least-crowded candidate,
    // which here is one step off the bearing, not the untouched projected point, and every member computes the same.
    {
        constexpr float kPi = 3.1415926536f;
        const float spacingYd = 10.0f;
        const float minYd = SpreadSlotMinRangeYd(5.0f, 0.0f);
        std::vector<SpreadSlotPoint> slots;
        slots.push_back(SpreadSlotPoint{ 10.0f, 0.0f, 1, 0 });  // the hunter's canonical ring slot
        for (int k = 0; k < 10; ++k)
        {
            const float angle = static_cast<float>(k) * 39.0f * kPi / 180.0f;
            SpreadSlotPoint caster;
            caster.x = minYd * std::cos(angle);
            caster.y = minYd * std::sin(angle);
            caster.ring = 2;
            caster.j = k;
            slots.push_back(caster);
        }
        std::vector<char> minRangeByIndex(slots.size(), 0);
        minRangeByIndex[0] = 1;
        const std::vector<SpreadSlotPoint> owners =
            SpreadSlotOwnerPoints(slots, minRangeByIndex, slots.size(), 0.0f, 0.0f, 0.0f, 0.0f, minYd, spacingYd);
        const auto apart = [](const SpreadSlotPoint& a, const SpreadSlotPoint& b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        };
        const auto clearance = [&](const SpreadSlotPoint& point) {
            float nearest = 1.0e30f;
            for (std::size_t j = 1; j < slots.size(); ++j)
                nearest = std::min(nearest, apart(point, slots[j]));
            return nearest;
        };
        const SpreadSlotPoint start = SpreadSlotMinRangePoint(slots[0], 0.0f, 0.0f, 0.0f, 0.0f, minYd);
        const float step = SpreadSlotSpacingAngle(spacingYd, minYd);
        CHECK(step > 0.0f);
        const float bearing = std::atan2(start.y, start.x);
        float bestClear = clearance(start);
        CHECK(bestClear < spacingYd);  // the untouched projected point is crowded too
        for (int t = 1; t <= 8; ++t)
        {
            const float sign = (t % 2 == 1) ? 1.0f : -1.0f;
            const float angle = bearing + sign * static_cast<float>((t + 1) / 2) * step;
            const SpreadSlotPoint candidate{ minYd * std::cos(angle), minYd * std::sin(angle), 1, 0 };
            const float clear = clearance(candidate);
            CHECK(clear < spacingYd);  // no candidate clears: the fallback has to choose
            bestClear = std::max(bestClear, clear);
        }
        CHECK(clearance(owners[0]) >= bestClear - 0.001f);
        CHECK(owners[0].x != start.x || owners[0].y != start.y);
        // Every member recomputes the same fallback point.
        const std::vector<SpreadSlotPoint> rebuilt =
            SpreadSlotOwnerPoints(slots, minRangeByIndex, slots.size(), 0.0f, 0.0f, 0.0f, 0.0f, minYd, spacingYd);
        CHECK(rebuilt[0].x == owners[0].x && rebuilt[0].y == owners[0].y);
    }
    std::cout << "  [PASS] a crowded min-range circle takes the least-crowded candidate\n";

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

    // The verdict precedence of the slot decision, in D6 order. The last argument is the dead-zone guard: it sits
    // before the floor (the dead-zone point is what makes the slot unusable, so a floor answer would hide it) and
    // before the range test.
    CHECK(SpreadSlotWanted(true, true, true, true, false, false, false, false, true, true, false) ==
          SpreadSlotVerdict::Seek);
    CHECK(SpreadSlotWanted(false, true, true, true, true, true, true, true, false, false, false) ==
          SpreadSlotVerdict::Stand);
    CHECK(SpreadSlotWanted(false, false, true, true, true, true, true, true, false, false, false) ==
          SpreadSlotVerdict::Victim);
    CHECK(SpreadSlotWanted(false, false, false, true, true, true, true, true, false, false, false) ==
          SpreadSlotVerdict::Walkout);
    CHECK(SpreadSlotWanted(false, false, false, false, false, true, true, true, false, false, false) ==
          SpreadSlotVerdict::NoCentre);
    CHECK(SpreadSlotWanted(false, false, false, false, true, false, true, true, false, false, false) ==
          SpreadSlotVerdict::NoSlot);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, false, true, false, false, false) ==
          SpreadSlotVerdict::Floor);
    // SpreadSlot_DeadZoneVerdict: a slot inside the shot's dead zone leaves the read with no usable point (the zone
    // is what refused it), so the dead zone outranks the floor; it also outranks the range test and the walk, and
    // the hunter keeps the step-out's position.
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, false, true, false, false, true) ==
          SpreadSlotVerdict::DeadZone);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, false, false, false, false, true) ==
          SpreadSlotVerdict::DeadZone);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, false, false, false, false) ==
          SpreadSlotVerdict::Range);
    // A usable point outside the zone is still a walk.
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, true, false, false, false) ==
          SpreadSlotVerdict::SlotMove);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, false, false, false, false) !=
          SpreadSlotVerdict::DeadZone);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, true, false, true, false) ==
          SpreadSlotVerdict::SlotHold);
    CHECK(SpreadSlotWanted(false, false, false, false, true, true, true, true, true, false, false) ==
          SpreadSlotVerdict::Cast);
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::SlotHold)) == "slot_hold");
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::NoCentre)) == "no_centre");
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::DeadZone)) == "dead_zone");
    CHECK(std::string(SpreadSlotVerdictWord(SpreadSlotVerdict::MoveFailed)) == "move_failed");
    std::cout << "  [PASS] slot verdict precedence and words\n";

    // SpreadVerdict_OwnerByKeyState: the row has one writer per key state,
    // so the slot read and the old step-away never share its 5 s window.
    CHECK(SpreadLogOwned(true, SpreadLogCaller::Read));
    CHECK(!SpreadLogOwned(true, SpreadLogCaller::Legacy));
    CHECK(!SpreadLogOwned(false, SpreadLogCaller::Read));
    CHECK(SpreadLogOwned(false, SpreadLogCaller::Legacy));
    std::cout << "  [PASS] one spread-row owner per key state\n";

    // A heal is never cut; a damage dealer cuts after 6 s of blocked reads.
    CHECK(!SpreadSlotCastCutDue(true, 1000u, 100000u));
    CHECK(!SpreadSlotCastCutDue(false, 0u, 100000u));
    CHECK(!SpreadSlotCastCutDue(false, 5000u, 5000u + 5999u));
    CHECK(SpreadSlotCastCutDue(false, 5000u, 5000u + 6000u));
    CHECK(SpreadSlotCastCutDue(false, 0xFFFFFF00u, 0xFFFFFF00u + 6000u));
    std::cout << "  [PASS] only a damage dealer's cast is cut, after 6 s\n";

    // SpreadSlot_CentreKindDistinct: the row's centre word separates a live anchor, a seek spot and
    // neither, so a no_centre read is not logged as a spot read.
    CHECK(SpreadCentreFor(true, true) == SpreadCentreKind::Anchor);
    CHECK(SpreadCentreFor(true, false) == SpreadCentreKind::Anchor);
    CHECK(SpreadCentreFor(false, true) == SpreadCentreKind::Spot);
    CHECK(SpreadCentreFor(false, false) == SpreadCentreKind::None);
    CHECK(std::string(SpreadCentreWord(SpreadCentreKind::Anchor)) == "anchor");
    CHECK(std::string(SpreadCentreWord(SpreadCentreKind::Spot)) == "spot");
    CHECK(std::string(SpreadCentreWord(SpreadCentreKind::None)) == "none");
    std::cout << "  [PASS] the centre kind keeps anchor, spot and none distinct\n";

    // SpreadSlot_CastClockRestartsAfterGap: the action only runs on cast, slot_move and slot_hold, so a stamp left
    // by another verdict (or the previous pull) must not cut the first cast after a gather. A gap of more than
    // 1.5 s between two cast reads starts the run over.
    {
        SpreadCastClock clock;
        CHECK(SpreadSlotCastStart(clock, 0u) == 1u);
        CHECK(SpreadSlotCastStart(clock, 3000u) == 3000u);
        CHECK(SpreadSlotCastStart(clock, 33000u) == 33000u);
        CHECK(!SpreadSlotCastCutDue(false, clock.sinceMs, 33000u + 5999u));
        CHECK(SpreadSlotCastCutDue(false, clock.sinceMs, 33000u + 6000u));
    }
    std::cout << "  [PASS] the cast clock restarts after a gap\n";

    // RaidSpreadAction::isUseful's stay gate. The base movement gate refuses
    // a bot whose current state holds a stay; only that stay can be
    // exempted, and only with the key on and a live spread row. A stay in
    // the current state with no live row still refuses the step, and a bot
    // the base gate did not refuse is never exempt, whatever row is live
    // (the combat-state stay is not the refusal being answered).
    CHECK(!ShouldSpreadStayVeto(false, true));
    CHECK(!ShouldSpreadStayVeto(false, false));
    CHECK(ShouldSpreadStayVeto(true, false));

    CHECK(!IsRaidSpreadStayExempt(false, true, true));
    CHECK(!IsRaidSpreadStayExempt(false, true, false));
    CHECK(!IsRaidSpreadStayExempt(false, false, true));
    CHECK(!IsRaidSpreadStayExempt(false, false, false));

    CHECK(IsRaidSpreadStayExempt(true, true, true));
    CHECK(!IsRaidSpreadStayExempt(true, true, false));
    CHECK(!IsRaidSpreadStayExempt(true, false, true));
    CHECK(!IsRaidSpreadStayExempt(true, false, false));
    std::cout << "  [PASS] raid spread stay gate reads the key, the current-state stay and the live row\n";
    std::cout << "TortoiseBots combat-spread policy tests passed.\n";
    return 0;
}
