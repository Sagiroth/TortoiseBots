#pragma once

// Pure decision rules for issue #470 (combat spread + flee memory).
// Ported donor behaviour (mod-playerbots @ b6696bdb):
//   MovementAction::CheckLastFlee (src/Ai/Base/Actions/MovementActions.cpp)
//   + CombatFormationMoveAction::Execute (nearest-group-member step-out)
//   + MoveFromGroupStrategy (move-from-group relevance).
// Adapted: the donor stores a "recently flee info" FleeInfo list value and an
// opt-in "disperse distance" knob; here the last two flee headings live on
// LastMovement (no extra value plumbing) and spread is trigger-gated.
// Issue #486 replaces dispatch history with observed failures only.
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_combat_spread_policy.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    // Empty-slot marker for failure memory. 10 rad exceeds any
    // real heading (valid range -PI..PI), mirroring the donor's -1 default
    // for "disperse distance" (disabled).
    constexpr float kFleeAngleEmpty = 10.0f;

    // Donor veto half-width (CheckLastFlee: fabs(revAngle - curAngle) < PI/4):
    // a candidate heading within 45 degrees of a failed destination is
    // deprioritized. A successful escape remains repeatable.
    constexpr float kFleeAngleVetoHalfWidth = 0.7853981634f; // PI/4

    // How many flee headings are remembered. Two matches the donor's
    // effective window (list capped at 10 but pruned to ~5 s of entries).
    constexpr std::size_t kFleeAngleSlots = 2;

    // Spread re-step floor in milliseconds: one spread step-out per window
    // per bot. Without it stacked ranged bots ping-pong every tick (each
    // step toward open ground lands next to another friendly and re-fires).
    // 3 s mirrors the flee return-delay scale without touching it.
    constexpr std::uint32_t kSpreadStepCooldownMs = 3000;

    // "Good enough" spacing: below this the trigger may fire, at/above it a
    // fresh trigger check treats the bot as spread and holds position.
    // Matches the RaidSpreadNeeded/Action 10 yd stack radius.
    constexpr float kSpreadSettledDistance = 10.0f;

    // Fixed ranged slots between the doses: each ranged bot and healer owns a
    // point on rings around the boss anchor, the spread row's yards apart, and
    // stands on it whenever the seek does not need it. Arrival is this close
    // (2D); a slot further than the target reach from the bot's fight target is
    // left alone.
    constexpr float kSpreadSlotArriveYd = 2.0f;
    constexpr float kSpreadSlotMaxTargetYd = 30.0f;
    // The largest ring stays this far inside the anchor's circle.
    constexpr float kSpreadSlotAnchorMarginYd = 2.0f;
    // A damage dealer stops its own cast for the walk after this long; a healer
    // never does.
    constexpr std::uint32_t kSpreadSlotCastWaitMs = 6000;
    // A `cast` read this long after the previous one starts a new run: the
    // action only runs on cast, slot_move and slot_hold, so the stamp left by
    // another verdict (or the previous pull) must not cut the first cast.
    constexpr std::uint32_t kSpreadSlotCastGapMs = 1500;
    constexpr int kSpreadSlotMaxRings = 3;
    // A hunter's Auto Shot has this dead zone: nearer than 8 yd it cannot shoot.
    constexpr float kSpreadMinRangeShotYd = 8.0f;
    // The floor's own margin past the arrival slack: a hunter standing `kSpreadSlotArriveYd` inside its point is still
    // this far outside the dead zone.
    constexpr float kSpreadMinRangeMarginYd = 1.0f;
    // The dead-zone guard's margin past the shot's minimum range: a slot this near its target cannot be shot from, so
    // walking to it cancels the shot and hands the position back to the step-out.
    constexpr float kSpreadSlotMinRangeMarginYd = 2.0f;
    // The grid the owner-point search rounds the boss position to: every member reads the same map even when the boss
    // drifted a fraction of a yard between two of their reads.
    constexpr float kSpreadSlotBossGridYd = 1.0f;

    struct SpreadSlotPoint
    {
        float x = 0.0f;
        float y = 0.0f;
        int ring = 0;
        int j = 0;
    };

    // The largest ring that still fits the anchor: ring k has radius
    // k * spacing and must stay the margin inside the circle. Two rings hold
    // without an anchor (the seek spot is the centre), never above the cap.
    inline int SpreadSlotRings(float spacingYd, bool anchored, float anchorRadiusYd)
    {
        if (spacingYd <= 0.0f)
            return 0;
        if (!anchored)
            return kSpreadSlotMaxRings > 2 ? 2 : kSpreadSlotMaxRings;
        int rings = 0;
        for (int k = 1; k <= kSpreadSlotMaxRings; ++k)
            if (static_cast<float>(k) * spacingYd <= anchorRadiusYd - kSpreadSlotAnchorMarginYd)
                rings = k;
        return rings;
    }

    // Ring k has floor(2*PI*k) points (6, 12, 18) at radius k * spacing, its
    // own half-step offset ring by ring, so the rings interleave.
    inline std::vector<SpreadSlotPoint> SpreadSlotPoints(float cx, float cy, float spacingYd, int rings)
    {
        std::vector<SpreadSlotPoint> points;
        if (spacingYd <= 0.0f)
            return points;
        constexpr float kPi = 3.1415926536f;
        for (int k = 1; k <= rings; ++k)
        {
            const int slots = static_cast<int>(std::floor(2.0f * kPi * static_cast<float>(k)));
            const float radius = static_cast<float>(k) * spacingYd;
            const float offset = static_cast<float>(k - 1) * kPi / 12.0f;
            for (int j = 0; j < slots; ++j)
            {
                const float angle = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slots) + offset;
                SpreadSlotPoint point;
                point.x = cx + radius * std::cos(angle);
                point.y = cy + radius * std::sin(angle);
                point.ring = k;
                point.j = j;
                points.push_back(point);
            }
        }
        return points;
    }

    // Nearest to the preference point first; equal distances keep the
    // (ring, j) order of the first sort, so the tie always picks the lower j.
    inline void SpreadSlotOrder(std::vector<SpreadSlotPoint>& points, float prefX, float prefY)
    {
        std::stable_sort(points.begin(), points.end(), [](const SpreadSlotPoint& a, const SpreadSlotPoint& b) {
            if (a.ring != b.ring)
                return a.ring < b.ring;
            return a.j < b.j;
        });
        std::stable_sort(points.begin(), points.end(), [prefX, prefY](const SpreadSlotPoint& a, const SpreadSlotPoint& b) {
            const float ax = a.x - prefX;
            const float ay = a.y - prefY;
            const float bx = b.x - prefX;
            const float by = b.y - prefY;
            return ax * ax + ay * ay < bx * bx + by * by;
        });
    }

    // Whether the min-range floor applies to this bot: the key is on and the bot's class needs a shot's minimum
    // range (a hunter's Auto Shot has an 8 yd dead zone). Casters have no dead zone, so their slots stay on the
    // rings even with the key on.
    inline bool SpreadSlotMinRangeApplies(bool keyOn, bool needsMinRange)
    {
        return keyOn && needsMinRange;
    }

    // Whether a slot point lies inside the shot's dead zone for its current target: the point's distance to the
    // target centre, less the reach between the two bodies, is the distance the shot would cover, and it must clear
    // the shot's minimum range by the margin. A min range of 0 (a class without a dead zone) never bites.
    inline bool SpreadSlotInsideMinRange(float slotToTargetCentreYd, float reachYd, float minRangeYd)
    {
        return minRangeYd > 0 && slotToTargetCentreYd - reachYd < minRangeYd + kSpreadSlotMinRangeMarginYd;
    }

    // The floor a min-range slot keeps from the boss: the boss's body, the shot's dead zone, the bot's own body, the
    // arrival slack (an arrived hunter stands up to that far inside its point) and the margin, so an arrived hunter is
    // never inside the dead zone. A nonsense reach reads as 0 so the floor never drops below the dead zone.
    inline float SpreadSlotMinRangeYd(float bossCombatReachYd, float playerCombatReachYd)
    {
        const float boss = std::isfinite(bossCombatReachYd) && bossCombatReachYd > 0.0f ? bossCombatReachYd : 0.0f;
        const float self =
            std::isfinite(playerCombatReachYd) && playerCombatReachYd > 0.0f ? playerCombatReachYd : 0.0f;
        return boss + kSpreadMinRangeShotYd + self + kSpreadSlotArriveYd + kSpreadMinRangeMarginYd;
    }

    // The boss position the owner-point search uses, snapped down to the grid: two members that read the boss a
    // fraction of a yard apart still build one map.
    inline float SpreadSlotBossGrid(float yd)
    {
        if (!std::isfinite(yd))
            return 0.0f;
        return std::floor(yd / kSpreadSlotBossGridYd) * kSpreadSlotBossGridYd;
    }

    // A ring point nearer the boss than `minYd` moves out along its own bearing from the boss to exactly that
    // range; a point already that far, or one on the boss itself (bearing taken from the centre instead, +y when
    // that is degenerate too), keeps its ring and slot. The rings have no floor from the boss, so without this a
    // hunter's slot lands inside Auto Shot's dead zone and the step-out fights the pull back.
    inline SpreadSlotPoint SpreadSlotMinRangePoint(const SpreadSlotPoint& point, float bossX, float bossY,
        float centreX, float centreY, float minYd)
    {
        if (!(minYd > 0.0f))
            return point;
        float dx = point.x - bossX;
        float dy = point.y - bossY;
        float yd = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(yd) || yd >= minYd)
            return point;
        if (yd < 0.01f)
        {
            dx = point.x - centreX;
            dy = point.y - centreY;
            yd = std::sqrt(dx * dx + dy * dy);
            if (yd < 0.01f)
            {
                dx = 0.0f;
                dy = 1.0f;
                yd = 1.0f;
            }
        }
        SpreadSlotPoint out = point;
        out.x = bossX + dx / yd * minYd;
        out.y = bossY + dy / yd * minYd;
        return out;
    }

    // The angle between two points on a circle of `radiusYd` that are `spacingYd` apart: the arc step a min-range
    // scan uses so each candidate is one spacing from the last.
    inline float SpreadSlotSpacingAngle(float spacingYd, float radiusYd)
    {
        if (!(spacingYd > 0.0f) || !(radiusYd > 0.0f))
            return 0.0f;
        const float ratio = spacingYd / (2.0f * radiusYd);
        return 2.0f * std::asin(ratio >= 1.0f ? 1.0f : ratio);
    }

    // Every slot owner's final point, resolved the same way on every bot. A caster keeps its canonical slot. A
    // min-range hunter's point is projected out to the min-range circle, then stepped along it by alternating
    // ±k spacing angles until it is at least the row's spacing from every other OWNED point (an earlier owner's final
    // point, a later owner's projected start; a caster's point is both), up to 8 steps. An unowned ring point is
    // nobody's place to stand: it is empty ground and never pushes a hunter off its bearing. When no candidate
    // clears, the least-crowded one wins and ties keep the earliest. The index order is the guid order, so the same
    // group yields the same points on every member.
    inline std::vector<SpreadSlotPoint> SpreadSlotOwnerPoints(const std::vector<SpreadSlotPoint>& canonical,
        const std::vector<char>& minRangeByIndex, std::size_t ownerCount, float bossX, float bossY, float centreX,
        float centreY, float minYd, float spacingYd)
    {
        const std::size_t owners = std::min(std::min(canonical.size(), minRangeByIndex.size()), ownerCount);
        std::vector<SpreadSlotPoint> start(owners);
        std::vector<SpreadSlotPoint> final(owners);
        for (std::size_t i = 0; i < owners; ++i)
            start[i] = minRangeByIndex[i] ? SpreadSlotMinRangePoint(canonical[i], bossX, bossY, centreX, centreY,
                                                                     minYd)
                                          : canonical[i];
        const float step = SpreadSlotSpacingAngle(spacingYd, minYd);
        for (std::size_t i = 0; i < owners; ++i)
        {
            const bool moved = start[i].x != canonical[i].x || start[i].y != canonical[i].y;
            if (!moved || step <= 0.0f)
            {
                final[i] = start[i];
                continue;
            }
            const auto clearYd = [&](const SpreadSlotPoint& point) {
                float nearest = 1.0e30f;
                for (std::size_t j = 0; j < owners; ++j)
                {
                    if (j == i)
                        continue;
                    const SpreadSlotPoint& other = j < i ? final[j] : start[j];
                    const float dx = point.x - other.x;
                    const float dy = point.y - other.y;
                    nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy));
                }
                return nearest;
            };
            SpreadSlotPoint best = start[i];
            float bestClear = clearYd(start[i]);
            if (bestClear >= spacingYd)
            {
                final[i] = best;
                continue;
            }
            const float bearing = std::atan2(start[i].y - bossY, start[i].x - bossX);
            for (int t = 1; t <= 8; ++t)
            {
                const float sign = (t % 2 == 1) ? 1.0f : -1.0f;
                const float angle = bearing + sign * static_cast<float>((t + 1) / 2) * step;
                SpreadSlotPoint candidate = start[i];
                candidate.x = bossX + minYd * std::cos(angle);
                candidate.y = bossY + minYd * std::sin(angle);
                const float clear = clearYd(candidate);
                if (clear > bestClear)
                {
                    best = candidate;
                    bestClear = clear;
                }
                if (clear >= spacingYd)
                {
                    best = candidate;
                    break;
                }
            }
            final[i] = best;
        }
        return final;
    }

    // A bot's index in the slot order: the number of slot-eligible members
    // with a lower raw guid. -1 when the bot is not one of them.
    inline int SpreadSlotIndexOf(const std::vector<std::uint64_t>& eligibleGuids, std::uint64_t self)
    {
        bool found = false;
        int index = 0;
        for (const std::uint64_t guid : eligibleGuids)
        {
            if (guid == self)
                found = true;
            else if (guid < self)
                ++index;
        }
        return found ? index : -1;
    }

    // The spare-slot fallback when the owned slot (or its fallback) is unusable:
    // one of the slots past the eligible members, keyed by the same index.
    inline int SpreadSlotFallback(int index, int count, int total)
    {
        if (index < 0 || count <= 0 || total <= count)
            return -1;
        return count + index % (total - count);
    }

    // The point the bot walks to and the telemetry it reports: the row keeps the
    // owned slot and its distance even when the spare point is what is stood on.
    struct SpreadSlotSelection
    {
        int slot = -1;      // the owned slot
        int moveSlot = -1;  // the point to stand on
        int fallback = -1;  // the spare point on the row, -1 when unused
    };

    inline SpreadSlotSelection SpreadSlotSelect(int index, int fallback, bool ownedUsable, bool fallbackUsable)
    {
        SpreadSlotSelection selection;
        selection.slot = index >= 0 ? index : -1;
        if (ownedUsable && index >= 0)
            selection.moveSlot = index;
        else if (fallback >= 0 && fallbackUsable)
        {
            selection.moveSlot = fallback;
            selection.fallback = fallback;
        }
        return selection;
    }

    enum class SpreadSlotVerdict
    {
        SlotHold,
        SlotMove,
        Seek,
        Stand,
        Victim,
        Walkout,
        Range,
        Cast,
        Floor,
        DeadZone,
        NoSlot,
        NoCentre,
        MoveFailed,
    };

    inline const char* SpreadSlotVerdictWord(SpreadSlotVerdict verdict)
    {
        switch (verdict)
        {
            case SpreadSlotVerdict::SlotHold: return "slot_hold";
            case SpreadSlotVerdict::SlotMove: return "slot_move";
            case SpreadSlotVerdict::Seek: return "seek";
            case SpreadSlotVerdict::Stand: return "stand";
            case SpreadSlotVerdict::Victim: return "victim";
            case SpreadSlotVerdict::Walkout: return "walkout";
            case SpreadSlotVerdict::Range: return "range";
            case SpreadSlotVerdict::Cast: return "cast";
            case SpreadSlotVerdict::Floor: return "floor";
            case SpreadSlotVerdict::DeadZone: return "dead_zone";
            case SpreadSlotVerdict::NoSlot: return "no_slot";
            case SpreadSlotVerdict::NoCentre: return "no_centre";
            case SpreadSlotVerdict::MoveFailed: return "move_failed";
        }
        return "no_centre";
    }

    // The slot decision's precedence: the seek and the stand move the bot, a
    // fight duty keeps it, then the geometry. Arrival outranks the cast gate
    // (a bot at its slot holds there, cast or not). A slot inside the shot's
    // dead zone is no place to walk to, so it outranks the floor, the range
    // test and the walk and leaves the position to the bot's own step-out:
    // the dead-zone point is also what makes the slot unusable, so read after
    // the floor it could never be named.
    inline SpreadSlotVerdict SpreadSlotWanted(bool seekValid, bool standValid, bool victimOrCharmed, bool walkout,
        bool hasCentre, bool hasSlot, bool slotUsable, bool inRange, bool castBlocked, bool arrived, bool deadZone)
    {
        if (seekValid)
            return SpreadSlotVerdict::Seek;
        if (standValid)
            return SpreadSlotVerdict::Stand;
        if (victimOrCharmed)
            return SpreadSlotVerdict::Victim;
        if (walkout)
            return SpreadSlotVerdict::Walkout;
        if (!hasCentre)
            return SpreadSlotVerdict::NoCentre;
        if (!hasSlot)
            return SpreadSlotVerdict::NoSlot;
        if (deadZone)
            return SpreadSlotVerdict::DeadZone;
        if (!slotUsable)
            return SpreadSlotVerdict::Floor;
        if (!inRange)
            return SpreadSlotVerdict::Range;
        if (arrived)
            return SpreadSlotVerdict::SlotHold;
        if (castBlocked)
            return SpreadSlotVerdict::Cast;
        return SpreadSlotVerdict::SlotMove;
    }

    // Who writes the BossTacticsSpread row. The slot read owns it with the
    // key on; the key off belongs to the old step-away, whose gate words are
    // the row's only content then. One writer per key state keeps the two
    // paths off each other's 5 s window.
    enum class SpreadLogCaller
    {
        Read,
        Legacy,
    };

    inline bool SpreadLogOwned(bool slotsKey, SpreadLogCaller caller)
    {
        return slotsKey ? caller == SpreadLogCaller::Read : caller == SpreadLogCaller::Legacy;
    }

    // Which centre a slot read worked from: the live anchor, the fight's seek spot when no anchor
    // exists, or neither (the `no_centre` verdict). The row names it so a scan that missed the boss
    // for a tick shows up instead of passing as a spot read.
    enum class SpreadCentreKind
    {
        Anchor,
        Spot,
        None,
    };

    inline SpreadCentreKind SpreadCentreFor(bool anchored, bool seekSpot)
    {
        if (anchored)
            return SpreadCentreKind::Anchor;
        return seekSpot ? SpreadCentreKind::Spot : SpreadCentreKind::None;
    }

    inline const char* SpreadCentreWord(SpreadCentreKind kind)
    {
        switch (kind)
        {
            case SpreadCentreKind::Anchor: return "anchor";
            case SpreadCentreKind::Spot: return "spot";
            case SpreadCentreKind::None: return "none";
        }
        return "none";
    }

    // The `cast` run behind SpreadSlotCastCutDue: its start and when the last cast
    // verdict was read. A read more than kSpreadSlotCastGapMs after the last one
    // starts the run over.
    struct SpreadCastClock
    {
        std::uint32_t sinceMs = 0;
        std::uint32_t seenMs = 0;
    };

    inline std::uint32_t SpreadSlotCastStart(SpreadCastClock& clock, std::uint32_t nowMs)
    {
        const std::uint32_t now = nowMs != 0 ? nowMs : 1;
        if (clock.sinceMs == 0 || now - clock.seenMs > kSpreadSlotCastGapMs)
            clock.sinceMs = now;
        clock.seenMs = now;
        return clock.sinceMs;
    }

    // A heal never waits for the cast; a damage dealer cuts its own cast after
    // the wait, once per unbroken run of blocked reads.
    inline bool SpreadSlotCastCutDue(bool isHealer, std::uint32_t castSinceMs, std::uint32_t nowMs)
    {
        if (isHealer || castSinceMs == 0)
            return false;
        return nowMs - castSinceMs >= kSpreadSlotCastWaitMs;
    }

    // Circular heading distance in [0, PI]: wraps the raw difference into a
    // full turn first, then mirrors past PI.
    inline float FleeHeadingDistance(float a, float b)
    {
        float diff = a > b ? a - b : b - a;
        while (diff > 6.2831853072f)
            diff -= 6.2831853072f;
        if (diff > 3.1415926536f)
            diff = 6.2831853072f - diff;
        return diff;
    }

    // A dispatch is not an outcome. Observe separation from the same anchor
    // for up to 5 s; 2 yd gained clears the attempt, while no gain after 3 s
    // remembers its heading for 5 s. Flee and spread use separate instances.
    class FleeFailureMemory
    {
    public:
        static constexpr std::uint32_t kObservationMs = 3000;
        static constexpr std::uint32_t kMaxObservationMs = 5000;
        static constexpr std::uint32_t kFailureTtlMs = 5000;
        static constexpr float kSeparationGain = 2.0f;

        void Clear() { *this = FleeFailureMemory(); }
        std::uint64_t Anchor() const { return anchor; }
        bool IsPending() const { return pending; }

        void Observe(std::uint64_t newAnchor, std::uint32_t newMap,
            float distance, std::uint32_t nowMs, std::uint32_t currentSpline = 0)
        {
            if (anchor != newAnchor || map != newMap)
            {
                Clear();
                anchor = newAnchor;
                map = newMap;
            }
            if (!pending)
                return;
            if (spline != currentSpline)
            {
                pending = false; // Interrupted/replaced movement has no attributable outcome.
                return;
            }
            std::uint32_t const elapsed = nowMs - startedMs;
            if (!std::isfinite(distance) || elapsed > kMaxObservationMs)
            {
                pending = false; // No timely evidence: do not invent a failure.
                return;
            }
            if (distance >= startDistance + kSeparationGain)
            {
                // A fallback may have succeeded in a previously failed heading.
                for (auto& failure : failures)
                    if (FleeHeadingDistance(heading, failure.angle) < kFleeAngleVetoHalfWidth)
                        failure = Failure();
                pending = false;
            }
            else if (elapsed >= kObservationMs)
            {
                RecordFailure(heading, nowMs);
                pending = false;
            }
        }

        void BeginAttempt(std::uint64_t newAnchor, std::uint32_t newMap,
            float newHeading, float distance, std::uint32_t nowMs, std::uint32_t newSpline = 0)
        {
            Observe(newAnchor, newMap, distance, nowMs, spline);
            if (!newAnchor || !std::isfinite(newHeading) || !std::isfinite(distance))
                return;
            newHeading = std::remainder(newHeading, 6.2831853072f);
            // Frequent re-dispatches of the same vector must not restart the
            // observation clock and hide a bot that is making no progress.
            if (pending && FleeHeadingDistance(heading, newHeading) < kFleeAngleVetoHalfWidth)
            {
                spline = newSpline;
                return;
            }
            spline = newSpline;
            pending = true;
            heading = newHeading;
            startDistance = distance;
            startedMs = nowMs;
        }

    private:
        void RecordFailure(float failedHeading, std::uint32_t nowMs)
        {
            if (!anchor || !std::isfinite(failedHeading))
                return;
            failedHeading = std::remainder(failedHeading, 6.2831853072f);
            if (FleeHeadingDistance(failedHeading, failures[0].angle) >= kFleeAngleVetoHalfWidth)
                failures[1] = failures[0];
            failures[0] = { failedHeading, nowMs };
        }

    public:
        bool IsHeadingFree(float candidate, std::uint32_t nowMs) const
        {
            for (auto const& failure : failures)
                if (failure.angle != kFleeAngleEmpty &&
                    (nowMs - failure.atMs) < kFailureTtlMs &&
                    FleeHeadingDistance(candidate, failure.angle) < kFleeAngleVetoHalfWidth)
                    return false;
            return true;
        }

    private:
        struct Failure
        {
            float angle = kFleeAngleEmpty;
            std::uint32_t atMs = 0;
        };
        Failure failures[kFleeAngleSlots];
        std::uint64_t anchor = 0;
        std::uint32_t map = 0;
        bool pending = false;
        float heading = 0.0f;
        float startDistance = 0.0f;
        std::uint32_t startedMs = 0;
        std::uint32_t spline = 0;
    };

    // Combat-spread gate for the "raid spread needed" trigger: spread applies
    // only while fighting, never for owned/hired bots answering to a real
    // player master (their player decides positioning), and never against an
    // explicit hold order (stay/follow/wait-for-attack/grind anchors).
    inline bool ShouldCombatSpread(bool inCombat, bool hasRealPlayerMaster,
        bool stayOrdered, bool followOrdered, bool waitOrdered, bool grindOrdered)
    {
        if (!inCombat)
            return false;
        if (hasRealPlayerMaster)
            return false;
        return !stayOrdered && !followOrdered && !waitOrdered && !grindOrdered;
    }

    // Owned-bot answer for Execute-time gates: module-owned (hired/owned,
    // IsOwnedBot) counts as owned even when the master is offline or on
    // another character, where HasRealPlayerMaster goes false. Pool bots
    // (IsRandomBot, no owner record) stay eligible.
    inline bool IsSpreadExemptOwned(bool hasRealPlayerMaster, bool isOwnedBot)
    {
        return hasRealPlayerMaster || isOwnedBot;
    }

    // Default ("raid spread") gate for the universal trigger and for
    // encounter scripts that queue the step-out directly (Onyxia phase 2).
    // The owned/hired exemption (#479) keeps a player's companions on their
    // player in the open world and in 5-man dungeons. On a raid map spread is
    // raid survival (Fireball splash, chain damage), so it applies to
    // companions too. Combat-only and explicit hold orders still win there.
    inline bool ShouldRaidSurvivalSpread(bool inCombat, bool hasRealPlayerMaster,
        bool isOwnedBot, bool onRaidMap,
        bool stayOrdered, bool followOrdered, bool waitOrdered, bool grindOrdered)
    {
        if (!onRaidMap && IsSpreadExemptOwned(hasRealPlayerMaster, isOwnedBot))
            return false;
        return ShouldCombatSpread(inCombat, false,
            stayOrdered, followOrdered, waitOrdered, grindOrdered);
    }

    // The stay term the universal spread gate receives: a live spread row outranks the stay order (the run holds the
    // ranged bots on their spots, and the row's splash still has to be answered there), so the order only vetoes
    // while no row widened the spacing. RaidSpreadNeededTrigger::IsActive and RaidSpreadAction::Execute read the
    // order through this one construction.
    inline bool ShouldSpreadStayVeto(bool stayOrdered, bool spreadLive)
    {
        return stayOrdered && !spreadLive;
    }

    // RaidSpreadAction::isUseful's stay gate. The base movement gate refuses a bot whose CURRENT state holds a stay
    // (MovementAction::isUseful reads ai->GetState()), so the stay read here must come from that same state: a stay
    // kept in another state is not the refusal being answered. With AiPlayerbot.RaidReactions on, a live spread row
    // outranks that stay exactly as the trigger and Execute read the order through ShouldSpreadStayVeto, so the
    // refusal is overruled only then; a stay with no live row still refuses the step. Off: today's logic.
    inline bool IsRaidSpreadStayExempt(bool raidReactions, bool stayInCurrentState, bool spreadLive)
    {
        return raidReactions && stayInCurrentState && spreadLive;
    }

    // Spread re-step throttle: a step-out within the cooldown of the last
    // recorded flee/spread dispatch holds position instead of stepping again.
    // Wrap-safe via unsigned subtraction; 0 (no dispatch yet) never throttles.
    inline bool IsSpreadOnCooldown(std::uint32_t nowMs, std::uint32_t lastStepMs,
        std::uint32_t cooldownMs = kSpreadStepCooldownMs)
    {
        if (lastStepMs == 0)
            return false;
        return (nowMs - lastStepMs) < cooldownMs;
    }

    // Opt-in spread gate ("spread" strategy): the player asked for spacing,
    // so owned/hired bots and melee are eligible too. Still combat-only and
    // never against an explicit hold order (stay/follow/guard/
    // wait-for-attack/grind anchors) — explicit orders beat automation
    // even when opted in. Note: follow/grind only exist on NON_COMBAT, so
    // those two params are near-dead checks kept for symmetry with the
    // legacy gate; stay/guard/wait-for-attack do the real work in combat.
    inline bool ShouldOptInSpread(bool inCombat,
        bool stayOrdered, bool followOrdered, bool guardOrdered, bool waitOrdered, bool grindOrdered)
    {
        if (!inCombat)
            return false;
        return !stayOrdered && !followOrdered && !guardOrdered && !waitOrdered && !grindOrdered;
    }

    // Donor "disperse distance" defaults (DisperseSetAction enable/reset):
    // 5yd for ranged, 2yd for melee. A set "spread distance" value (> 0)
    // overrides both; unset (-1) keeps the role default.
    constexpr float kSpreadDistanceRangedYd = 5.0f;
    constexpr float kSpreadDistanceMeleeYd = 2.0f;

    // Effective "too close" radius: the manual knob when set, else the role
    // default. Non-positive manual values mean unset (donor: dis <= 0 off).
    inline float SpreadRadius(float manualDistance, bool isRanged)
    {
        if (manualDistance > 0.0f)
            return manualDistance;
        return isRanged ? kSpreadDistanceRangedYd : kSpreadDistanceMeleeYd;
    }
} // namespace ai
