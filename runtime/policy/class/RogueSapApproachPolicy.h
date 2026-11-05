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
constexpr int kSapMaxPathQueries = 48;  // navmesh queries per plan
constexpr float kSapPackRadius = 30.0f; // hostiles this close to the target are its pack
constexpr float kSapStealthAlertYd = 5.0f;
constexpr float kSapClearanceMargin = 2.0f;
constexpr unsigned kSapReplanMs = 2000;  // the plan is rebuilt from where the rogue stands
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
// the rogue), and only those outside every clearance count. The first one whose
// navmesh path from the rogue stays clear wins. Failing that, the rogue may walk
// round the pack: a detour point on a ring of waypointRadius around the target,
// clear itself, with a clear path from the rogue to it and from it to a stand
// point. No such route: found stays false and the Sap is skipped.
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

    // No pack: the plain walk to the rogue's side of the mob, no queries.
    if (mates.empty())
    {
        plan.found = true;
        plan.stand = stands.front();
        return plan;
    }

    auto clearPath = [&](SapPoint from, SapPoint to) -> bool
    {
        if (plan.pathQueries >= kSapMaxPathQueries)
            return false;
        ++plan.pathQueries;
        SapPath path;
        if (!pathFn(from, to, path))
            return false;
        if (path.empty() || SapDist(path.back(), to) > 1.0f)
            path.push_back(to);
        return SapPathClear(from, path, mates);
    };

    for (SapPoint const& s : stands)
    {
        if (clearPath(start, s))
        {
            plan.found = true;
            plan.stand = s;
            return plan;
        }
    }

    std::vector<SapPoint> waypoints;
    for (SapPoint const& w : SapRing(target, waypointRadius, baseAngle, kSapWaypointAngles))
        if (SapPointMargin(w, mates) >= 0.0f)
            waypoints.push_back(w);

    // A detour point the rogue cannot reach cleanly is not asked again for the next stand point.
    std::vector<int> reach(waypoints.size(), -1);
    for (SapPoint const& s : stands)
    {
        std::vector<std::size_t> order(waypoints.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
        {
            return SapDist(waypoints[a], s) < SapDist(waypoints[b], s);
        });
        for (std::size_t i : order)
        {
            if (plan.pathQueries >= kSapMaxPathQueries)
                return plan;
            if (reach[i] < 0)
                reach[i] = clearPath(start, waypoints[i]) ? 1 : 0;
            if (reach[i] == 0)
                continue;
            if (clearPath(waypoints[i], s))
            {
                plan.found = true;
                plan.viaWaypoint = true;
                plan.waypoint = waypoints[i];
                plan.stand = s;
                return plan;
            }
        }
    }
    return plan;
}

} // namespace TortoiseBots
