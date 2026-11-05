#pragma once

// Pre-pull Sap approach around the target's pack (AiPlayerbot.SapApproachAvoidPack,
// default 1). Pure 2D geometry with no core types: ReachSapAction
// (ai/playerbot/strategy/rogue/RogueActions.cpp) turns the target, its pack-mates
// and the navmesh into these inputs; tools/test_rogue_sap_approach_policy.cpp
// tests the rules on their own.
//
// A stealthed rogue is only aggroed by a creature that detects it: the core calls
// the creature AI's MoveInLineOfSight only for a unit it can see
// (CallAIMoveLOS -> IsVisibleForOrDetect -> CanDetectStealthOf), so the radius that
// matters for each pack-mate is its stealth detection range (front arc, the worse
// case), plus the five yards in which it turns to face the rogue (the "alert"
// range), plus a margin. The plain aggro radius (about 18 yd at equal level) would
// rule out every Sap: Sap is a melee-range spell and pack-mates stand a few yards
// from the target.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace TortoiseBots
{

struct SapPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// Centre and the centre-to-centre distance the rogue keeps from it.
struct SapPackMate
{
    float x = 0.0f;
    float y = 0.0f;
    float clearance = 0.0f;
};

using SapPath = std::vector<SapPoint>;

constexpr int kSapStandAngles = 16;     // stand points tried around the target
constexpr int kSapWaypointAngles = 16;  // detour points tried on the outer ring
constexpr int kSapMaxCandidates = 8;    // stand points, and detour routes, tried per plan
constexpr int kSapMaxPathQueries = 16;  // navmesh queries per plan
constexpr float kSapPackRadius = 30.0f; // hostiles this close to the target are its pack
constexpr float kSapStealthAlertYd = 5.0f;
constexpr float kSapClearanceMargin = 2.0f;
constexpr unsigned kSapReplanMs = 5000;  // a plan is kept at most this long
constexpr float kSapReplanMoveYd = 3.0f; // a cached "no approach" is retried once a pack-mate moves this far
// A cached approach is re-proved on every use against the pack as it stands now
// (SapPlanStale); it is dropped once the target moves this far (the stand point
// would leave Sap range: ReachSapAction keeps at least 1 yd of slack) or the rogue
// strays this far from the path that was proved.
constexpr float kSapReplanTargetYd = 0.5f;
constexpr float kSapRouteOffYd = 1.5f;
constexpr unsigned kSapSkipMs = 30000;   // a skipped Sap is not tried again on that mob for this long
// "manual int" keys that carry a skip from ReachSapAction to the "sap cc mark" trigger.
constexpr char const* kSapSkipTargetKey = "sap skip target";
constexpr char const* kSapSkipUntilKey = "sap skip until ms";

// A Sap skipped on this mob (its guid counter) is still in force.
inline bool SapSkipActive(unsigned skippedCounter, unsigned targetCounter, unsigned untilMs, unsigned nowMs)
{
    return skippedCounter != 0 && skippedCounter == targetCounter &&
        static_cast<int>(untilMs - nowMs) > 0;
}

// Unit::CanDetectStealthOf for a creature looking at a stealthed player in its
// front arc: 5/6 yd plus 1/6 yd per point of detection over stealth (doubled when
// the creature is more than three levels above the rogue), clamped to 0-30 yd.
inline float SapStealthDetectRange(int detectorLevel, int detectBonus, int stealthSkill, int levelDiff)
{
    float yardsPerLevel = 5.0f / 6.0f;
    if (levelDiff > 3)
        yardsPerLevel *= 2.0f;
    float const visible = 5.0f / 6.0f +
        static_cast<float>(detectorLevel * 5 + detectBonus - stealthSkill) * yardsPerLevel / 5.0f;
    return std::min(30.0f, std::max(0.0f, visible));
}

// Centre-to-centre clearance: the core measures detection edge to edge, so both
// bounding radii are added on top of detection, alert range and margin.
inline float SapStealthClearance(float detectRange, float radiiSum)
{
    return detectRange + kSapStealthAlertYd + kSapClearanceMargin + radiiSum;
}

inline float SapDist(SapPoint a, SapPoint b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

inline float SapSegmentDist(SapPoint a, SapPoint b, SapPoint p)
{
    float const dx = b.x - a.x;
    float const dy = b.y - a.y;
    float const lenSq = dx * dx + dy * dy;
    float t = 0.0f;
    if (lenSq > 0.0f)
        t = std::min(1.0f, std::max(0.0f, ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq));
    return SapDist(SapPoint{a.x + t * dx, a.y + t * dy}, p);
}

// Smallest (distance - clearance) over the pack; >= 0 means outside every clearance.
inline float SapPointMargin(SapPoint p, std::vector<SapPackMate> const& mates)
{
    float margin = std::numeric_limits<float>::infinity();
    for (SapPackMate const& m : mates)
        margin = std::min(margin, SapDist(p, SapPoint{m.x, m.y}) - m.clearance);
    return margin;
}

// The walk start -> path[0] -> ... -> path.back() stays outside every clearance.
// A rogue that already stands inside one (undetected) may walk on, but never
// closer to that mate than it is now.
inline bool SapPathClear(SapPoint start, SapPath const& path, std::vector<SapPackMate> const& mates)
{
    for (SapPackMate const& m : mates)
    {
        SapPoint const c{m.x, m.y};
        float const allowed = std::min(m.clearance, SapDist(start, c) - 0.01f);
        SapPoint prev = start;
        for (SapPoint const& p : path)
        {
            if (SapSegmentDist(prev, p, c) < allowed)
                return false;
            prev = p;
        }
    }
    return true;
}

struct SapApproachPlan
{
    bool found = false;
    bool viaWaypoint = false;
    SapPoint waypoint;
    SapPoint stand;
    // The path the plan proved clear, after the planning start: every navmesh leg
    // to the detour point (if any) and on to the stand point.
    SapPath route;
    int pathQueries = 0;
};

inline std::vector<SapPoint> SapRing(SapPoint centre, float radius, float baseAngle, int count)
{
    std::vector<SapPoint> ring;
    ring.reserve(static_cast<std::size_t>(count));
    float const step = 6.2831853f / static_cast<float>(count);
    for (int i = 0; i < count; ++i)
    {
        float const a = baseAngle + step * static_cast<float>(i);
        ring.push_back(SapPoint{centre.x + radius * std::cos(a), centre.y + radius * std::sin(a)});
    }
    return ring;
}

// Picks where the rogue stands to Sap and how it gets there.
//
// Stand points sit standDist from the target, ordered by their distance from the
// pack (the far side first; ties, as with no pack at all, go to the one nearest
// the rogue); only those outside every clearance count, at most kSapMaxCandidates.
// The first one whose navmesh path from the rogue stays clear wins. Failing that,
// the rogue may walk round the pack: a detour point on a ring of waypointRadius
// around the target, clear itself, with a clear path from the rogue to it and from
// it to a stand point. Detour routes whose straight legs are already clear are
// tried first (on open floor the navmesh path is the straight line), at most
// kSapMaxCandidates of them. No route, or the kSapMaxPathQueries budget spent:
// found stays false and the Sap is skipped.
//
// pathFn(from, to, out) fills `out` with the navmesh path (any points after
// `from`) and returns false when `to` cannot be reached.
template <class PathFn>
SapApproachPlan PlanSapApproach(SapPoint start, SapPoint target, float standDist, float waypointRadius,
    std::vector<SapPackMate> const& mates, PathFn&& pathFn)
{
    SapApproachPlan plan;
    float const baseAngle = std::atan2(start.y - target.y, start.x - target.x);

    std::vector<SapPoint> stands;
    for (SapPoint const& p : SapRing(target, standDist, baseAngle, kSapStandAngles))
        if (SapPointMargin(p, mates) >= 0.0f)
            stands.push_back(p);
    std::stable_sort(stands.begin(), stands.end(), [&](SapPoint const& a, SapPoint const& b)
    {
        float const ma = SapPointMargin(a, mates);
        float const mb = SapPointMargin(b, mates);
        if (ma != mb)
            return ma > mb;
        return SapDist(start, a) < SapDist(start, b);
    });
    if (stands.empty())
        return plan;
    if (stands.size() > static_cast<std::size_t>(kSapMaxCandidates))
        stands.resize(static_cast<std::size_t>(kSapMaxCandidates));

    // No pack: the plain walk to the rogue's side of the mob, no queries.
    if (mates.empty())
    {
        plan.found = true;
        plan.stand = stands.front();
        plan.route.push_back(plan.stand);
        return plan;
    }

    auto clearPath = [&](SapPoint from, SapPoint to, SapPath& path) -> bool
    {
        ++plan.pathQueries;
        path.clear();
        if (!pathFn(from, to, path))
            return false;
        if (path.empty() || SapDist(path.back(), to) > 1.0f)
            path.push_back(to);
        return SapPathClear(from, path, mates);
    };

    for (SapPoint const& s : stands)
    {
        if (plan.pathQueries >= kSapMaxPathQueries)
            return plan;
        SapPath path;
        if (clearPath(start, s, path))
        {
            plan.found = true;
            plan.stand = s;
            plan.route = path;
            return plan;
        }
    }

    std::vector<SapPoint> waypoints;
    for (SapPoint const& w : SapRing(target, waypointRadius, baseAngle, kSapWaypointAngles))
        if (SapPointMargin(w, mates) >= 0.0f)
            waypoints.push_back(w);

    struct Detour
    {
        std::size_t w;
        std::size_t s;
        bool straightClear;
        float length;
    };
    std::vector<Detour> detours;
    for (std::size_t wi = 0; wi < waypoints.size(); ++wi)
    {
        bool const firstLegStraight = SapPathClear(start, SapPath{waypoints[wi]}, mates);
        for (std::size_t si = 0; si < stands.size(); ++si)
        {
            bool const straight = firstLegStraight && SapPathClear(waypoints[wi], SapPath{stands[si]}, mates);
            detours.push_back(Detour{wi, si, straight,
                SapDist(start, waypoints[wi]) + SapDist(waypoints[wi], stands[si])});
        }
    }
    std::stable_sort(detours.begin(), detours.end(), [](Detour const& a, Detour const& b)
    {
        if (a.straightClear != b.straightClear)
            return a.straightClear;
        return a.length < b.length;
    });
    if (detours.size() > static_cast<std::size_t>(kSapMaxCandidates))
        detours.resize(static_cast<std::size_t>(kSapMaxCandidates));

    // A detour point the rogue cannot reach cleanly is not asked again.
    std::vector<int> reach(waypoints.size(), -1);
    std::vector<SapPath> reachPath(waypoints.size());
    for (Detour const& d : detours)
    {
        if (reach[d.w] < 0)
        {
            if (plan.pathQueries >= kSapMaxPathQueries)
                return plan;
            reach[d.w] = clearPath(start, waypoints[d.w], reachPath[d.w]) ? 1 : 0;
        }
        if (reach[d.w] == 0)
            continue;
        if (plan.pathQueries >= kSapMaxPathQueries)
            return plan;
        SapPath second;
        if (clearPath(waypoints[d.w], stands[d.s], second))
        {
            plan.found = true;
            plan.viaWaypoint = true;
            plan.waypoint = waypoints[d.w];
            plan.stand = stands[d.s];
            plan.route = reachPath[d.w];
            plan.route.insert(plan.route.end(), second.begin(), second.end());
            return plan;
        }
    }
    return plan;
}

// One plan per Sap order: kept for at most kSapReplanMs and while it still holds
// on every use. A found plan is re-proved against the pack as it stands now (no
// navmesh query): the rogue is still on the path that was proved (within
// kSapRouteOffYd), the rest of that path and the stand point stay outside every
// current clearance, the pack has the same size and the target has not moved
// kSapReplanTargetYd. It is rebuilt once the rogue reaches its detour point (the
// next leg starts there). A cached "no approach" is retried when the target or a
// pack-mate moves kSapReplanMoveYd.
struct SapPlanCache
{
    bool valid = false;
    unsigned targetId = 0;
    SapPoint startAt;
    SapPoint targetAt;
    std::vector<SapPoint> matesAt;
    unsigned planMs = 0;
    SapApproachPlan plan;
};

inline bool SapPlanStale(SapPlanCache const& cache, unsigned targetId, SapPoint targetAt,
    std::vector<SapPackMate> const& mates, SapPoint here, unsigned nowMs)
{
    if (!cache.valid || cache.targetId != targetId)
        return true;
    if (nowMs - cache.planMs >= kSapReplanMs)
        return true;
    if (cache.matesAt.size() != mates.size())
        return true;

    if (cache.plan.found)
    {
        if (SapDist(cache.targetAt, targetAt) > kSapReplanTargetYd)
            return true;
        if (cache.plan.viaWaypoint && SapDist(here, cache.plan.waypoint) < 2.0f)
            return true;
        SapPath line;
        line.push_back(cache.startAt);
        line.insert(line.end(), cache.plan.route.begin(), cache.plan.route.end());
        std::size_t best = 0;
        float bestDist = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i + 1 < line.size(); ++i)
        {
            float const d = SapSegmentDist(line[i], line[i + 1], here);
            if (d < bestDist)
            {
                bestDist = d;
                best = i;
            }
        }
        if (bestDist > kSapRouteOffYd)
            return true;
        SapPath rest(line.begin() + static_cast<std::ptrdiff_t>(best) + 1, line.end());
        if (!SapPathClear(here, rest, mates))
            return true;
        return SapPointMargin(cache.plan.stand, mates) < 0.0f;
    }

    if (SapDist(cache.targetAt, targetAt) > kSapReplanMoveYd)
        return true;
    // The grid search returns the pack in no fixed order: every mate must still
    // stand within kSapReplanMoveYd of a cached position.
    for (SapPackMate const& m : mates)
    {
        bool hasNearbyCachedMate = false;
        for (SapPoint const& c : cache.matesAt)
            if (SapDist(c, SapPoint{m.x, m.y}) <= kSapReplanMoveYd)
            {
                hasNearbyCachedMate = true;
                break;
            }
        if (!hasNearbyCachedMate)
            return true;
    }
    return false;
}

// The cached plan, or a new one when SapPlanStale says so. `replanned` tells the
// caller whether pathFn ran this call.
template <class PathFn>
SapApproachPlan const& SapPlanFor(SapPlanCache& cache, unsigned targetId, SapPoint here, SapPoint targetAt,
    float standDist, float waypointRadius, std::vector<SapPackMate> const& mates, unsigned nowMs,
    PathFn&& pathFn, bool* replanned = nullptr)
{
    bool const stale = SapPlanStale(cache, targetId, targetAt, mates, here, nowMs);
    if (replanned)
        *replanned = stale;
    if (stale)
    {
        cache.plan = PlanSapApproach(here, targetAt, standDist, waypointRadius, mates, pathFn);
        cache.valid = true;
        cache.targetId = targetId;
        cache.startAt = here;
        cache.targetAt = targetAt;
        cache.matesAt.clear();
        for (SapPackMate const& m : mates)
            cache.matesAt.push_back(SapPoint{m.x, m.y});
        cache.planMs = nowMs;
    }
    return cache.plan;
}

} // namespace TortoiseBots
