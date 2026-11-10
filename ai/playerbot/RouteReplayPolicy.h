// Pure policy: how a dispatched travel leg replays its stored route over
// the live navmesh (MovementAction::DispatchMovement, multi-point branch).
//
// A stored leg (ai_playerbot_travelnode_path, shipped data computed on
// another server's mesh) goes straight into one spline today, so corners cut
// through walls and points float off the mesh. The replay asks this server's
// navmesh for a path from the bot to a stored point ahead and walks the
// mesh's answer instead, so the stored route only says where to go.
//
// Tested in tools/test_route_replay_policy.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    // How the goal is picked when the full stretch cannot be replayed: halve
    // the index into the leg a bounded number of times and try each nearer
    // stored point. The stretch's tail halves toward the head, so the first
    // fallback goal still covers most of the leg.
    constexpr std::uint32_t kRouteReplayHalvings = 2;

    // A mesh answer far longer than the stored stretch means the mesh goes
    // around something the stored route crosses (a jump down, a gap): walk
    // the stored stretch as it is instead of the detour.
    constexpr float kRouteReplayDetourRatio = 3.0f;

    // What the leg replay decided (RouteReplayDecide).
    enum class RouteReplayVerdict : std::uint8_t
    {
        WalkMeshPath = 0, // a goal ahead is reachable: walk the mesh's path
        WalkStored = 1,   // no goal ahead is reachable: keep today's behaviour
    };

    // A mesh goal answer the dispatch measured (RouteReplayDecide).
    struct RouteReplayProbe
    {
        bool reachable = false; // the mesh path reaches the goal
        float meshLengthYd = 0.0f; // the mesh path's own length
        float storedLengthYd = 0.0f; // bot to the goal along the stored points
        bool lastPoint = false; // the answer concerns the leg's first goal
    };

    // A goal ahead is kept when the mesh reaches it and the answer is not an
    // absurd detour around the stored stretch (a jump down, a gap): the
    // player-like walk holds the stored line, not the mesh around it.
    inline bool RouteReplayAcceptsProbe(RouteReplayProbe const& probe)
    {
        if (!probe.reachable || probe.meshLengthYd <= 0.0f || probe.storedLengthYd <= 0.0f)
            return false;
        return probe.meshLengthYd <= kRouteReplayDetourRatio * probe.storedLengthYd;
    }

    // Which points to ask the mesh about, longest stretch first: the leg's
    // last point, then the index halved toward the head, kRouteReplayHalvings
    // times. Skips the head point (the bot stands on it). Returns leg indices
    // into the dispatched points (past the bot).
    inline std::vector<std::size_t> RouteReplayGoals(std::size_t pointCount)
    {
        std::vector<std::size_t> goals;
        if (pointCount < 2)
            return goals;
        std::size_t goal = pointCount - 1;
        for (std::uint32_t tried = 0; tried <= kRouteReplayHalvings; ++tried)
        {
            if (goal < 1)
                break;
            goals.push_back(goal);
            goal /= 2;
        }
        return goals;
    }

    // Whether the leg may be replayed at all: a route the hazards rewrote
    // keeps its own bent shape, and a mesh answer over a water or missing
    // leg must never replace the stored points (legs through water, over
    // missing mesh, special legs).
    inline bool RouteReplayApplies(bool hazardRewrotePath, bool botInWater)
    {
        return !hazardRewrotePath && !botInWater;
    }
}
